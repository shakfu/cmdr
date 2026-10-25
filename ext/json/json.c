/* JSON kept as text, read and built by path. Readers return slices of their
 * input, so numbers keep their spelling. See docs/dev/ext-json.md. */
#include "cmdr.h"
#include "../utf8/utf8.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* Deeper nesting is an error, so hostile input cannot exhaust the C stack. */
#define DEPTH_LIMIT 512

typedef struct {
    const char *at;
    size_t n;
} span;

typedef struct {
    char *data;
    size_t length, capacity;
} buffer;

static void put(buffer *b, const char *text, size_t n)
{
    if (b->length + n + 1 > b->capacity) {
        b->capacity = (b->length + n + 1) * 2;
        b->data = cmdr_realloc(b->data, b->capacity, 1);
    }
    memcpy(b->data + b->length, text, n);
    b->length += n;
    b->data[b->length] = '\0';
}

static void put_text(buffer *b, const char *text) { put(b, text, strlen(text)); }

static cmdr_value taken(buffer *b)
{
    return (cmdr_value){.kind = CMDR_TEXT, .text = b->data ? b->data : cmdr_strndup("", 0)};
}

static int space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static const char *ws(const char *p, const char *end)
{
    while (p < end && space(*p)) ++p;
    return p;
}

/* ws, in validated text, which ends at its NUL. */
static const char *ws0(const char *p)
{
    while (space(*p)) ++p;
    return p;
}

static void put_utf8(buffer *b, unsigned long c)
{
    char out[4];
    size_t n;
    if (c < 0x80) out[0] = (char)c, n = 1;
    else if (c < 0x800) out[0] = (char)(0xc0 | c >> 6), out[1] = (char)(0x80 | (c & 0x3f)), n = 2;
    else if (c < 0x10000) {
        out[0] = (char)(0xe0 | c >> 12), out[1] = (char)(0x80 | (c >> 6 & 0x3f));
        out[2] = (char)(0x80 | (c & 0x3f)), n = 3;
    } else {
        out[0] = (char)(0xf0 | c >> 18), out[1] = (char)(0x80 | (c >> 12 & 0x3f));
        out[2] = (char)(0x80 | (c >> 6 & 0x3f)), out[3] = (char)(0x80 | (c & 0x3f)), n = 4;
    }
    put(b, out, n);
}

static int hex4(const char *p, unsigned long *unit)
{
    *unit = 0;
    for (int i = 0; i < 4; ++i) {
        char c = p[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
              : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return 1;
        *unit = *unit << 4 | (unsigned long)d;
    }
    return 0;
}

/* Validation, RFC 8259. Each function returns the position after what it
 * scanned, or NULL with problem and where set. */
typedef struct {
    const char *end, *problem, *where;
} scanner;

static const char *fail(scanner *s, const char *where, const char *problem)
{
    s->where = where;
    s->problem = problem;
    return NULL;
}

static const char *scan_value(scanner *s, const char *p, int depth);

static const char *scan_string(scanner *s, const char *p)
{
    for (++p; p < s->end;) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') return p + 1;
        if (c < 0x20) return fail(s, p, "control character in string");
        if (c == '\\') {
            if (p + 1 >= s->end) break;
            char e = p[1];
            unsigned long unit;
            if (e == 'u') {
                if (s->end - p < 6 || hex4(p + 2, &unit)) return fail(s, p, "bad \\u escape");
                p += 6;
            } else if (e && strchr("\"\\/bfnrt", e)) p += 2;
            else return fail(s, p, "bad escape");
        } else {
            size_t n = utf8_length((const unsigned char *)p, (const unsigned char *)s->end);
            if (!n) return fail(s, p, "invalid UTF-8");
            p += n;
        }
    }
    return fail(s, p, "unterminated string");
}

static int digit(const char *p, const char *end) { return p < end && *p >= '0' && *p <= '9'; }

static const char *scan_number(scanner *s, const char *p)
{
    const char *start = p;
    if (p < s->end && *p == '-') ++p;
    if (!digit(p, s->end)) return fail(s, start, "bad number");
    if (*p == '0') ++p;
    else while (digit(p, s->end)) ++p;
    if (p < s->end && *p == '.') {
        if (!digit(++p, s->end)) return fail(s, start, "bad number");
        while (digit(p, s->end)) ++p;
    }
    if (p < s->end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < s->end && (*p == '+' || *p == '-')) ++p;
        if (!digit(p, s->end)) return fail(s, start, "bad number");
        while (digit(p, s->end)) ++p;
    }
    return p;
}

static const char *scan_container(scanner *s, const char *p, int depth)
{
    char close = *p == '[' ? ']' : '}';
    if (depth >= DEPTH_LIMIT) return fail(s, p, "nested too deeply");
    p = ws(p + 1, s->end);
    if (p < s->end && *p == close) return p + 1;
    for (;;) {
        if (close == '}') {
            if (p >= s->end || *p != '"') return fail(s, p, "expected a string key");
            if (!(p = scan_string(s, p))) return NULL;
            p = ws(p, s->end);
            if (p >= s->end || *p != ':') return fail(s, p, "expected ':'");
            p = ws(p + 1, s->end);
        }
        if (!(p = scan_value(s, p, depth + 1))) return NULL;
        p = ws(p, s->end);
        if (p < s->end && *p == close) return p + 1;
        if (p >= s->end || *p != ',') return fail(s, p, close == ']' ? "expected ',' or ']'" : "expected ',' or '}'");
        p = ws(p + 1, s->end);
    }
}

static const char *scan_value(scanner *s, const char *p, int depth)
{
    if (p >= s->end) return fail(s, p, "expected a value");
    switch (*p) {
    case '"': return scan_string(s, p);
    case '[': case '{': return scan_container(s, p, depth);
    case 't': case 'f': case 'n': {
        const char *word = *p == 't' ? "true" : *p == 'f' ? "false" : "null";
        size_t n = strlen(word);
        if ((size_t)(s->end - p) < n || memcmp(p, word, n)) return fail(s, p, "expected a value");
        return p + n;
    }
    default:
        if (*p == '-' || (*p >= '0' && *p <= '9')) return scan_number(s, p);
        return fail(s, p, "expected a value");
    }
}

/* Validate text as one JSON value and set *value to it, without surrounding
 * whitespace. */
static int parse(const char *name, const char *text, span *value, char **error)
{
    scanner s = {.end = text + strlen(text)};
    const char *p = ws(text, s.end), *after = scan_value(&s, p, 0);
    if (after && ws(after, s.end) != s.end) after = fail(&s, ws(after, s.end), "text after the value");
    if (!after) return cmdr_error(error, "%s: invalid JSON at byte %zu: %s", name, (size_t)(s.where - text), s.problem);
    *value = (span){p, (size_t)(after - p)};
    return 0;
}

/* The end of the validated value at p. */
static const char *skip(const char *p)
{
    if (*p == '"') {
        for (++p; *p != '"'; ++p)
            if (*p == '\\') ++p;
        return p + 1;
    }
    if (*p == '[' || *p == '{') {
        int depth = 0;
        do {
            if (*p == '"') { p = skip(p); continue; }
            if (*p == '[' || *p == '{') ++depth;
            else if (*p == ']' || *p == '}') --depth;
            ++p;
        } while (depth);
        return p;
    }
    while (*p && !strchr(",]} \t\n\r", *p)) ++p;
    return p;
}

/* Step through a validated array or object. *p starts just inside its
 * bracket. Sets key, for an object, and value, then returns 1; 0 at the end. */
static int next(const char **p, int object, span *key, span *value)
{
    const char *q = ws0(*p);
    if (*q == ',') q = ws0(q + 1);
    if (*q == ']' || *q == '}') return 0;
    if (object) {
        key->at = q;
        q = skip(q);
        key->n = (size_t)(q - key->at);
        q = ws0(ws0(q) + 1); /* After ':'. */
    }
    value->at = q;
    q = skip(q);
    value->n = (size_t)(q - value->at);
    *p = q;
    return 1;
}

static const char *type_of(span v)
{
    switch (*v.at) {
    case '{': return "object";
    case '[': return "array";
    case '"': return "string";
    case 't': case 'f': return "bool";
    case 'n': return "null";
    default: return "number";
    }
}

/* Decode a validated string token into UTF-8. Strict decoding, for text,
 * fails on NUL and lone surrogates; otherwise they are kept, a surrogate as
 * its three-byte form, so keys still compare. */
static int decode(span s, int strict, buffer *out, const char **problem)
{
    const char *p = s.at + 1, *end = s.at + s.n - 1;
    out->length = 0;
    put(out, "", 0);
    while (p < end) {
        if (*p != '\\') {
            const char *run = p;
            while (p < end && *p != '\\') ++p;
            put(out, run, (size_t)(p - run));
            continue;
        }
        char e = p[1];
        p += 2;
        if (e != 'u') {
            const char *from = "\"\\/bfnrt", *to = "\"\\/\b\f\n\r\t";
            put(out, to + (strchr(from, e) - from), 1);
            continue;
        }
        unsigned long c, low;
        hex4(p, &c);
        p += 4;
        if (c >= 0xd800 && c <= 0xdbff && end - p >= 6 && p[0] == '\\' && p[1] == 'u'
            && !hex4(p + 2, &low) && low >= 0xdc00 && low <= 0xdfff) {
            c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
            p += 6;
        } else if (strict && c >= 0xd800 && c <= 0xdfff) {
            *problem = "a lone surrogate cannot be text";
            return 1;
        }
        if (strict && !c) {
            *problem = "text cannot hold NUL";
            return 1;
        }
        put_utf8(out, c);
    }
    return 0;
}

/* One UTF-16 code unit or ASCII byte, escaped as Python's json.dumps does
 * with ensure_ascii. */
static void put_unit(buffer *b, unsigned long u)
{
    static const char from[] = "\"\\\n\r\t\b\f", to[] = "\"\\nrtbf";
    const char *at = u && u < 0x80 ? strchr(from, (int)u) : NULL;
    if (at) {
        char e[2] = {'\\', to[at - from]};
        put(b, e, 2);
    } else if (u >= 0x20 && u <= 0x7e) {
        char c = (char)u;
        put(b, &c, 1);
    } else {
        char e[8];
        snprintf(e, sizeof(e), "\\u%04x", (unsigned)(u & 0xffff));
        put(b, e, 6);
    }
}

static void put_code_point(buffer *b, unsigned long c)
{
    if (c < 0x10000) { put_unit(b, c); return; }
    c -= 0x10000;
    put_unit(b, 0xd800 + (c >> 10));
    put_unit(b, 0xdc00 + (c & 0x3ff));
}

/* TEXT as a JSON string, or 1 if it is not UTF-8. */
static int put_string(buffer *b, const char *text)
{
    const unsigned char *p = (const unsigned char *)text, *end = p + strlen(text);
    put(b, "\"", 1);
    while (p < end) {
        size_t n = utf8_length(p, end);
        if (!n) return 1;
        put_code_point(b, utf8_decode(p, n));
        p += n;
    }
    put(b, "\"", 1);
    return 0;
}

/* A validated string token, escaped again as json.dumps would. */
static void put_token(buffer *b, span s)
{
    const char *p = s.at + 1, *end = s.at + s.n - 1;
    put(b, "\"", 1);
    while (p < end) {
        if (*p == '\\') {
            unsigned long u;
            if (p[1] == 'u') hex4(p + 2, &u), p += 6;
            else {
                const char *from = "\"\\/bfnrt", *to = "\"\\/\b\f\n\r\t";
                u = (unsigned char)to[strchr(from, p[1]) - from];
                p += 2;
            }
            put_unit(b, u);
        } else {
            size_t n = utf8_length((const unsigned char *)p, (const unsigned char *)end);
            put_code_point(b, utf8_decode((const unsigned char *)p, n));
            p += n;
        }
    }
    put(b, "\"", 1);
}

/* An object's members as Python's json.loads keeps them: one per key, where
 * the key first appears, with the value of its last appearance. */
typedef struct {
    span key, value;
    buffer name; /* The key, decoded loosely. */
    size_t index;
    int dropped;
} member;

static int same_name(const member *x, const member *y)
{
    return x->name.length == y->name.length && !memcmp(x->name.data, y->name.data, x->name.length);
}

/* By name, then by position. */
static int by_name(const void *a, const void *b)
{
    const member *x = *(member *const *)a, *y = *(member *const *)b;
    size_t n = x->name.length < y->name.length ? x->name.length : y->name.length;
    int c = memcmp(x->name.data, y->name.data, n);
    if (c) return c;
    if (x->name.length != y->name.length) return x->name.length < y->name.length ? -1 : 1;
    return x->index < y->index ? -1 : x->index > y->index;
}

static member *members(span object, size_t *count)
{
    member *ms = NULL;
    size_t n = 0;
    const char *p = object.at + 1;
    span key, value;
    while (next(&p, 1, &key, &value)) {
        ms = cmdr_realloc(ms, n + 1, sizeof(*ms));
        ms[n] = (member){.key = key, .value = value, .index = n};
        const char *ignored;
        decode(key, 0, &ms[n].name, &ignored);
        ++n;
    }
    if (n > 1) {
        member **order = cmdr_realloc(NULL, n, sizeof(*order));
        for (size_t i = 0; i < n; ++i) order[i] = &ms[i];
        qsort(order, n, sizeof(*order), by_name);
        for (size_t i = 0, j; i < n; i = j) {
            for (j = i + 1; j < n && same_name(order[i], order[j]); ++j) order[j]->dropped = 1;
            order[i]->value = order[j - 1]->value;
        }
        free(order);
    }
    *count = n;
    return ms;
}

static void free_members(member *ms, size_t n)
{
    for (size_t i = 0; i < n; ++i) free(ms[i].name.data);
    free(ms);
}

static int is_index(const char *text, long long *index)
{
    const char *p = text + (*text == '-');
    if (!*p || strspn(p, "0123456789") != strlen(p)) return 0;
    errno = 0;
    *index = strtoll(text, NULL, 10);
    return !errno;
}

/* Set *out to the value at path in validated v. Returns CMDR_MISMATCH if a
 * key or index is missing, or 1 if a segment cannot apply. The last of
 * duplicate keys wins. */
static int walk(const char *name, span v, size_t n, const cmdr_value *path, span *out,
                char **error)
{
    for (size_t i = 0; i < n; ++i) {
        const char *seg = path[i].text;
        if (*v.at == '{') {
            const char *p = v.at + 1;
            span key, value, found = {0};
            buffer decoded = {0};
            size_t length = strlen(seg);
            while (next(&p, 1, &key, &value)) {
                const char *ignored;
                decode(key, 0, &decoded, &ignored);
                if (decoded.length == length && !memcmp(decoded.data, seg, length)) found = value;
            }
            free(decoded.data);
            if (!found.at) {
                char *d = cmdr_describe(&path[i]);
                cmdr_error(error, "%s: no key %s", name, d);
                free(d);
                return CMDR_MISMATCH;
            }
            v = found;
        } else if (*v.at == '[') {
            long long index;
            if (!is_index(seg, &index))
                return cmdr_error(error, "%s: array index is not an integer: %s", name, seg);
            const char *p = v.at + 1;
            span item;
            long long count = 0;
            while (next(&p, 0, NULL, &item)) ++count;
            if (index < 0) index += count;
            if (index < 0 || index >= count) {
                cmdr_error(error, "%s: index %s out of range for %lld items", name, seg, count);
                return CMDR_MISMATCH;
            }
            p = v.at + 1;
            for (long long k = 0; k <= index; ++k) next(&p, 0, NULL, &item);
            v = item;
        } else {
            return cmdr_error(error, "%s: cannot index a %s with %s", name, type_of(v), seg);
        }
    }
    *out = v;
    return 0;
}

/* Parse argv[0] and walk argv[1..]. */
static int at_path(const char *name, size_t argc, const cmdr_value *argv, span *v, char **error)
{
    span root;
    if (parse(name, argv[0].text, &root, error)) return 1;
    return walk(name, root, argc - 1, argv + 1, v, error);
}

static cmdr_value slice(span v) { return cmdr_text(v.at, v.n); }

COMMAND(get_command)
{
    if (argc < 1) return cmdr_error(error, "usage: json.get JSON [SEG ...]");
    span v;
    int status = at_path("json.get", argc, argv, &v, error);
    if (!status) *result = slice(v);
    return status;
}

COMMAND(text_command)
{
    if (argc < 1) return cmdr_error(error, "usage: json.text JSON [SEG ...]");
    span v;
    int status = at_path("json.text", argc, argv, &v, error);
    if (status) return status;
    if (*v.at != '"') return cmdr_error(error, "json.text: expected a string, got %s", type_of(v));
    buffer b = {0};
    const char *problem;
    if (decode(v, 1, &b, &problem)) {
        free(b.data);
        return cmdr_error(error, "json.text: %s", problem);
    }
    *result = taken(&b);
    return 0;
}

COMMAND(type_command)
{
    if (argc < 1) return cmdr_error(error, "usage: json.type JSON [SEG ...]");
    span v;
    int status = at_path("json.type", argc, argv, &v, error);
    if (!status) *result = cmdr_text(type_of(v), strlen(type_of(v)));
    return status;
}

COMMAND(has_command)
{
    if (argc < 1) return cmdr_error(error, "usage: json.has JSON [SEG ...]");
    span v;
    int status = at_path("json.has", argc, argv, &v, error);
    if (!status) *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return status;
}

static int items(int keys, const char *name, size_t argc, const cmdr_value *argv,
                 cmdr_value *result, char **error)
{
    span v;
    int status = at_path(name, argc, argv, &v, error);
    if (status) return status;
    if (*v.at != '{' && (keys || *v.at != '['))
        return cmdr_error(error, "%s: expected %s, got %s", name, keys ? "an object" : "an object or array", type_of(v));
    *result = (cmdr_value){.kind = CMDR_LIST};
    if (*v.at == '[') {
        const char *p = v.at + 1;
        span item;
        while (next(&p, 0, NULL, &item)) cmdr_push(result, slice(item));
        return 0;
    }
    size_t n;
    member *ms = members(v, &n);
    for (size_t i = 0; i < n && !status; ++i) {
        if (ms[i].dropped) continue;
        if (!keys) { cmdr_push(result, slice(ms[i].value)); continue; }
        buffer b = {0};
        const char *problem;
        if (decode(ms[i].key, 1, &b, &problem)) status = cmdr_error(error, "%s: %s", name, problem);
        else cmdr_push(result, cmdr_text(b.data, b.length));
        free(b.data);
    }
    free_members(ms, n);
    if (status) cmdr_free(result);
    return status;
}

COMMAND(keys_command)
{
    if (argc < 1) return cmdr_error(error, "usage: json.keys JSON [SEG ...]");
    return items(1, "json.keys", argc, argv, result, error);
}

COMMAND(values_command)
{
    if (argc < 1) return cmdr_error(error, "usage: json.values JSON [SEG ...]");
    return items(0, "json.values", argc, argv, result, error);
}

COMMAND(valid_command)
{
    if (argc != 1) return cmdr_error(error, "usage: json.valid TEXT");
    span v;
    if (parse("json.valid", argv[0].text, &v, error)) return CMDR_MISMATCH;
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return 0;
}

/* Building. */

COMMAND(str_command)
{
    if (argc != 1) return cmdr_error(error, "usage: json.str TEXT");
    buffer b = {0};
    if (put_string(&b, argv[0].text)) {
        free(b.data);
        return cmdr_error(error, "json.str: TEXT is not UTF-8");
    }
    *result = taken(&b);
    return 0;
}

/* Append text, which must be JSON, without surrounding whitespace. */
static int put_json(buffer *b, const char *name, const cmdr_value *value, char **error)
{
    span v;
    if (value->kind != CMDR_TEXT) return cmdr_error(error, "%s: expected JSON text", name);
    if (parse(name, value->text, &v, error)) return 1;
    put(b, v.at, v.n);
    return 0;
}

COMMAND(array_command)
{
    buffer b = {0};
    size_t n = 0;
    put(&b, "[", 1);
    for (size_t i = 0; i < argc; ++i) {
        int list = argv[i].kind == CMDR_LIST;
        const cmdr_value *xs = list ? argv[i].items : &argv[i];
        for (size_t j = 0; j < (list ? argv[i].count : 1); ++j) {
            if (n++) put(&b, ", ", 2);
            if (put_json(&b, "json.array", &xs[j], error)) { free(b.data); return 1; }
        }
    }
    put(&b, "]", 1);
    *result = taken(&b);
    return 0;
}

static int object(const char *name, int strings, size_t argc, const cmdr_value *argv,
                  cmdr_value *result, char **error)
{
    if (argc % 2) return cmdr_error(error, "usage: %s [KEY %s ...]", name, strings ? "TEXT" : "JSON");
    buffer b = {0};
    put(&b, "{", 1);
    for (size_t i = 0; i < argc; i += 2) {
        if (i) put(&b, ", ", 2);
        int status = put_string(&b, argv[i].text) ? cmdr_error(error, "%s: KEY is not UTF-8", name) : 0;
        put(&b, ": ", 2);
        if (!status && strings && put_string(&b, argv[i + 1].text))
            status = cmdr_error(error, "%s: TEXT is not UTF-8", name);
        if (!status && !strings) status = put_json(&b, name, &argv[i + 1], error);
        if (status) { free(b.data); return 1; }
    }
    put(&b, "}", 1);
    *result = taken(&b);
    return 0;
}

COMMAND(object_command) { return object("json.object", 0, argc, argv, result, error); }
COMMAND(strings_command) { return object("json.strings", 1, argc, argv, result, error); }

/* Splice VALUE into JSON at the path, keeping the rest as written. */
COMMAND(set_command)
{
    static const char usage[] = "usage: json.set JSON SEG [SEG ...] VALUE";
    if (argc < 3) return cmdr_error(error, usage);
    span root, parent, value;
    if (parse("json.set", argv[0].text, &root, error)) return 1;
    if (parse("json.set", argv[argc - 1].text, &value, error)) return 1;
    int status = walk("json.set", root, argc - 3, argv + 1, &parent, error);
    if (status) return status;
    const cmdr_value *last = &argv[argc - 2];
    const char *text = argv[0].text;
    buffer b = {0};
    if (*parent.at == '{') {
        const char *p = parent.at + 1, *end = NULL;
        span key, v, found = {0};
        buffer decoded = {0};
        while (next(&p, 1, &key, &v)) {
            const char *ignored;
            decode(key, 0, &decoded, &ignored);
            if (decoded.length == strlen(last->text) && !memcmp(decoded.data, last->text, decoded.length))
                found = v;
            end = v.at + v.n;
        }
        free(decoded.data);
        if (found.at) {
            put(&b, text, (size_t)(found.at - text));
            put(&b, value.at, value.n);
            put_text(&b, found.at + found.n);
        } else {
            /* A new key goes after the last member, or alone inside {}. */
            const char *at = end ? end : parent.at + 1;
            put(&b, text, (size_t)(at - text));
            if (end) put(&b, ", ", 2);
            if (put_string(&b, last->text)) {
                free(b.data);
                return cmdr_error(error, "json.set: key is not UTF-8");
            }
            put(&b, ": ", 2);
            put(&b, value.at, value.n);
            put_text(&b, end ? end : ws0(at));
        }
    } else {
        span item;
        if ((status = walk("json.set", parent, 1, last, &item, error))) return status;
        put(&b, text, (size_t)(item.at - text));
        put(&b, value.at, value.n);
        put_text(&b, item.at + item.n);
    }
    *result = taken(&b);
    return 0;
}

static void newline(buffer *b, long indent, size_t level)
{
    put(b, "\n", 1);
    for (size_t i = 0; i < (size_t)indent * level; ++i) put(b, " ", 1);
}

/* As json.dumps(json.loads(...), indent=indent); indent < 0 is one line. */
static void emit(buffer *b, span v, long indent, size_t level)
{
    if (*v.at == '"') { put_token(b, v); return; }
    if (*v.at != '[' && *v.at != '{') { put(b, v.at, v.n); return; }
    int object = *v.at == '{';
    size_t n = 0, count;
    member *ms = NULL;
    span item;
    const char *p = v.at + 1;
    if (object) ms = members(v, &count);
    put(b, object ? "{" : "[", 1);
    for (size_t i = 0;; ++i) {
        if (object) {
            if (i == count) break;
            if (ms[i].dropped) continue;
        } else if (!next(&p, 0, NULL, &item)) break;
        if (n++) put(b, indent < 0 ? ", " : ",", indent < 0 ? 2 : 1);
        if (indent >= 0) newline(b, indent, level + 1);
        if (object) {
            put_token(b, ms[i].key);
            put(b, ": ", 2);
            emit(b, ms[i].value, indent, level + 1);
        } else {
            emit(b, item, indent, level + 1);
        }
    }
    if (n && indent >= 0) newline(b, indent, level);
    put(b, object ? "}" : "]", 1);
    if (object) free_members(ms, count);
}

COMMAND(format_command)
{
    if (argc < 1 || argc > 2) return cmdr_error(error, "usage: json.format JSON [INDENT]");
    long indent = -1;
    if (argc == 2) {
        size_t n;
        if (cmdr_natural(&argv[1], &n, error)) return 1;
        if (n > 64) return cmdr_error(error, "json.format: INDENT %zu is above 64", n);
        indent = (long)n;
    }
    span v;
    if (parse("json.format", argv[0].text, &v, error)) return 1;
    buffer b = {0};
    emit(&b, v, indent, 0);
    *result = taken(&b);
    return 0;
}

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    TEXT("get", "get JSON [SEG ...]", "Return the value at the path, as JSON", get_command),
    TEXT("text", "text JSON [SEG ...]", "Return the string at the path, decoded", text_command),
    TEXT("type", "type JSON [SEG ...]", "Return object, array, string, number, bool, or null", type_command),
    TEXT("keys", "keys JSON [SEG ...]", "List the object's keys, decoded, in order", keys_command),
    TEXT("values", "values JSON [SEG ...]", "List the object's values or the array's items, as JSON", values_command),
    TEXT("has", "has JSON [SEG ...]", "Return JSON if the path exists", has_command),
    TEXT("valid", "valid TEXT", "Return TEXT if it is one valid JSON value", valid_command),
    TEXT("str", "str TEXT", "Return TEXT as a JSON string", str_command),
    {.name = "array", .usage = "array [JSON ...]",
     .docstring = "Return an array of the arguments; a list adds its items", .run = array_command},
    TEXT("object", "object [KEY JSON ...]", "Return an object of keys and JSON values", object_command),
    TEXT("strings", "strings [KEY TEXT ...]", "Return an object whose values are strings", strings_command),
    TEXT("set", "set JSON SEG [SEG ...] VALUE", "Return JSON with VALUE at the path", set_command),
    TEXT("format", "format JSON [INDENT]", "Reformat JSON, as Python's json.dumps", format_command),
};
const cmdr cmdr_ext_json = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
