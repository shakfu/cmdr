#include "cmdr.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Recursion limits: composed commands and block literals nest at most
 * MAX_CALL_DEPTH deep, and cmdr_clone rejects values nested more than
 * MAX_VALUE_DEPTH deep. Every stage clones its input or builds flat lists, so
 * value depth stays bounded. */
enum { MAX_CALL_DEPTH = 128, MAX_VALUE_DEPTH = 1024 };

static void (*out_of_memory)(void *data, size_t size);
static void *out_of_memory_data;

/* Atomic, not volatile sig_atomic_t: a signal handler sets it, and every
 * thread's cmdr_call reads it. */
static atomic_int interrupted;

void cmdr_interrupt(int on)
{
    interrupted = on != 0;
}

int cmdr_interrupted(void)
{
    return interrupted;
}

void cmdr_set_out_of_memory(void (*handler)(void *data, size_t size), void *data)
{
    out_of_memory = handler;
    out_of_memory_data = data;
}

void *cmdr_realloc(void *p, size_t count, size_t size)
{
    /* Never a zero-byte request, whose realloc result may be NULL. */
    if (!count) count = 1;
    if (!size) size = 1;
    int fits = count <= SIZE_MAX / size;
    if (fits && (p = realloc(p, count * size))) return p;
    if (out_of_memory) out_of_memory(out_of_memory_data, fits ? count * size : SIZE_MAX);
    fputs("cmdr: out of memory\n", stderr);
    exit(1);
}

char *cmdr_strndup(const char *text, size_t n)
{
    char *copy = cmdr_realloc(NULL, n + 1, 1);
    memcpy(copy, text, n);
    copy[n] = '\0';
    return copy;
}

cmdr_value cmdr_text(const char *text, size_t n)
{
    return (cmdr_value){.kind = CMDR_TEXT, .text = cmdr_strndup(text, n)};
}

void cmdr_push(cmdr_value *list, cmdr_value item)
{
    if (list->count >= list->capacity) {
        /* SIZE_MAX makes cmdr_realloc report the overflow. */
        size_t capacity = list->count < 4 ? 4 : list->count > SIZE_MAX / 2 ? SIZE_MAX : list->count * 2;
        list->items = cmdr_realloc(list->items, capacity, sizeof(*list->items));
        list->capacity = capacity;
    }
    list->items[list->count++] = item;
}

static cmdr_code *retain(cmdr_code *code);
static void release_code(cmdr_code *code);

void cmdr_free(cmdr_value *value)
{
    if (value->code) release_code(value->code);
    for (size_t i = 0; i < value->count; ++i) cmdr_free(&value->items[i]);
    free(value->items);
    free(value->text);
    *value = (cmdr_value){0};
}

static int clone(const cmdr_value *value, cmdr_value *out, unsigned depth, char **error)
{
    /* Check before retaining code, so a failure leaves nothing to release. */
    if (depth > MAX_VALUE_DEPTH) {
        *out = (cmdr_value){0};
        return cmdr_error(error, "value is nested too deeply");
    }
    *out = (cmdr_value){.kind = value->kind, .code = value->code ? retain(value->code) : NULL};
    if (value->text) out->text = cmdr_strndup(value->text, strlen(value->text));
    for (size_t i = 0; i < value->count; ++i) {
        cmdr_value item;
        if (clone(&value->items[i], &item, depth + 1, error)) {
            cmdr_free(out);
            return 1;
        }
        cmdr_push(out, item);
    }
    return 0;
}

int cmdr_clone(const cmdr_value *value, cmdr_value *out, char **error)
{
    return clone(value, out, 0, error);
}

void cmdr_render(const cmdr_value *value, FILE *out)
{
    if (value->kind == CMDR_TEXT) fputs(value->text, out);
    if (value->kind == CMDR_BLOCK) fprintf(out, "{%s}", value->text);
    for (size_t i = 0; value->kind == CMDR_LIST && i < value->count; ++i) {
        cmdr_render(&value->items[i], out);
        fputc('\n', out);
    }
}

typedef struct {
    char *data;
    size_t length;
} text_buffer;

static void append(text_buffer *b, const char *text, size_t n)
{
    b->data = cmdr_realloc(b->data, b->length + n + 1, 1);
    memcpy(b->data + b->length, text, n);
    b->data[b->length += n] = '\0';
}

/* Bounds on a description, so a file's text or a long list stays one line. */
enum { DESCRIBE_BYTES = 32, DESCRIBE_ITEMS = 5, DESCRIBE_DEPTH = 2 };

static void describe(const cmdr_value *value, unsigned depth, text_buffer *b)
{
    if (value->kind == CMDR_NONE) { append(b, "no value", 8); return; }
    if (value->kind == CMDR_LIST) {
        if (depth >= DESCRIBE_DEPTH) { append(b, "[...]", 5); return; }
        append(b, "[", 1);
        for (size_t i = 0; i < value->count && i < DESCRIBE_ITEMS; ++i) {
            if (i) append(b, ", ", 2);
            describe(&value->items[i], depth + 1, b);
        }
        if (value->count > DESCRIBE_ITEMS) {
            char more[48];
            append(b, more, (size_t)snprintf(more, sizeof(more), ", ... %zu items", value->count));
        }
        append(b, "]", 1);
        return;
    }
    const char *text = value->text;
    size_t n = strlen(text), shown = n < DESCRIBE_BYTES ? n : DESCRIBE_BYTES;
    int block = value->kind == CMDR_BLOCK;
    /* Quote text that would not read back as one word. */
    int quote = !block && (!n || strpbrk(text, " \t\n'\"\\,[]{}|"));
    append(b, block ? "{" : "'", (size_t)(block || quote));
    for (size_t i = 0; i < shown; ++i) {
        if (text[i] == '\n') append(b, "\\n", 2);
        else if (text[i] == '\t') append(b, "\\t", 2);
        else if (quote && (text[i] == '\'' || text[i] == '\\')) {
            append(b, "\\", 1);
            append(b, &text[i], 1);
        } else append(b, &text[i], 1);
    }
    if (n > shown) append(b, "...", 3);
    append(b, block ? "}" : "'", (size_t)(block || quote));
}

char *cmdr_describe(const cmdr_value *value)
{
    text_buffer b = {0};
    describe(value, 0, &b);
    return b.data;
}

int cmdr_error(char **error, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);
    size_t size = n < 0 ? 1 : (size_t)n + 1;
    *error = cmdr_realloc(NULL, size, 1);
    va_start(args, format);
    if (vsnprintf(*error, size, format, args) < 0) **error = '\0';
    va_end(args);
    return 1;
}

typedef struct {
    char *text;      /* Literal text, a $name, or block source. */
    int ref;         /* An unquoted $name; compile sets slot or constant. */
    size_t slot;     /* Index of the name in the environment. */
    const cmdr_value *constant; /* A command's value, when no block declares the name. */
    int block;       /* text is the source of a { ... } block. */
    cmdr_code *code; /* The block, compiled. */
    int sub;         /* text is the source of a $( ... ) sub-pipeline. */
    struct program *pipeline; /* The sub-pipeline, compiled. */
} word;

typedef struct {
    word *words;
    size_t count;
} stage;

typedef struct program {
    stage *stages;
    size_t count;
} program;

/* A compiled block, shared by every value made from it. */
struct cmdr_code {
    size_t refs;
    size_t params;   /* Declared parameters. */
    size_t required; /* Leading parameters without defaults. */
    char **defaults; /* Texts of the last params - required parameters. */
    int declared;    /* The source starts with "(...)", possibly empty. */
    const cmdr *scope; /* Names in body resolve here, where the block was written. */
    program body;
};

static void dispose(program *pg);

static cmdr_code *retain(cmdr_code *code)
{
    ++code->refs;
    return code;
}

static void free_defaults(cmdr_code *code)
{
    for (size_t i = 0; i < code->params - code->required; ++i) free(code->defaults[i]);
    free(code->defaults);
}

static void release_code(cmdr_code *code)
{
    if (--code->refs) return;
    dispose(&code->body);
    free_defaults(code);
    free(code);
}

static void dispose(program *pg)
{
    for (size_t i = 0; i < pg->count; ++i) {
        for (size_t j = 0; j < pg->stages[i].count; ++j) {
            word *w = &pg->stages[i].words[j];
            free(w->text);
            if (w->code) release_code(w->code);
            if (w->pipeline) {
                dispose(w->pipeline);
                free(w->pipeline);
            }
        }
        free(pg->stages[i].words);
    }
    free(pg->stages);
}

/* Length of the name at p: a letter, then letters, digits, '_' or '-'. */
static size_t identifier(const char *p)
{
    size_t n = 0;
    if (isalpha((unsigned char)*p))
        while (isalnum((unsigned char)p[n]) || p[n] == '_' || p[n] == '-') ++n;
    return n;
}

/* Lexemes: a word, a $name, a block, or a sub-pipeline. parse reads them
 * strictly and cmdr_scan tolerantly, so the parser, the REPL's highlighting,
 * and line splitting agree on where each one ends. */
typedef enum { LEX_WORD, LEX_REF, LEX_BLOCK, LEX_SUB } lexeme_kind;

typedef struct {
    lexeme_kind kind;
    size_t end;          /* Past the lexeme; n when a block or quote is unclosed. */
    size_t close;        /* A block's '}' or a sub-pipeline's ')', or n. */
    int plain;           /* A word without quotes or escapes. */
    int deep;            /* Brackets nest past MAX_CALL_DEPTH. */
    const char *problem; /* Why parse rejects it, or NULL. */
} lexeme;

/* Index of the bracket that closes the one before s[i], or n. Brackets of the
 * same kind nest; quotes and escapes hide them. Sets *deep past MAX_CALL_DEPTH. */
static size_t closing(const char *s, size_t i, size_t n, char open, char close, int *deep)
{
    char quote = 0;
    for (size_t depth = 1; i < n; ++i) {
        char c = s[i];
        if (c == '\\' && quote != '\'') { if (++i == n) break; }
        else if (quote) { if (c == quote) quote = 0; }
        else if (c == '\'' || c == '"') quote = c;
        else if (c == open && ++depth > MAX_CALL_DEPTH) { *deep = 1; return n; }
        else if (c == close && !--depth) return i;
    }
    return n;
}

/* Read the lexeme at s[i], i < n, not whitespace or '|'. Decode a word into
 * *text when text is non-NULL, and mark its quoted and escaped bytes in quoted
 * when quoted is non-NULL. */
static lexeme read_lexeme(const char *s, size_t i, size_t n, char **text, unsigned char *quoted)
{
    lexeme lx = {LEX_WORD, i, n, 1, 0, NULL};
    int sub = s[i] == '$' && i + 1 < n && s[i + 1] == '(';
    if (s[i] == '{' || sub) {
        lx.kind = sub ? LEX_SUB : LEX_BLOCK;
        lx.close = sub ? closing(s, i + 2, n, '(', ')', &lx.deep)
                       : closing(s, i + 1, n, '{', '}', &lx.deep);
        lx.end = lx.close < n ? lx.close + 1 : n;
        if (lx.deep) lx.problem = sub ? "sub-pipelines nested too deeply" : "blocks nested too deeply";
        else if (lx.close == n) lx.problem = sub ? "unclosed '$('" : "unclosed '{'";
        else if (lx.end < n && !isspace((unsigned char)s[lx.end]) && s[lx.end] != '|')
            lx.problem = sub ? "expected a space after ')'" : "expected a space after '}'";
        return lx;
    }
    if (s[i] == '$') {
        lx.kind = LEX_REF;
        while (lx.end < n && !isspace((unsigned char)s[lx.end]) && s[lx.end] != '|') ++lx.end;
        return lx;
    }
    size_t used = 0, capacity = 0;
    char quote = 0;
    while (lx.end < n && (quote || (!isspace((unsigned char)s[lx.end]) && s[lx.end] != '|'))) {
        size_t at = lx.end;
        char c = s[lx.end++];
        int literal = 1; /* c is part of the word's text, not a quote mark. */
        if (c == '\\' && quote != '\'') {
            lx.plain = 0;
            if (lx.end == n) { lx.problem = "unfinished escape"; break; }
            c = s[lx.end++];
            if (quote == '"' && c == 'n') c = '\n';
            else if (quote == '"' && c == 't') c = '\t';
        } else if (quote && c == quote) {
            quote = 0;
            literal = 0;
        } else if (!quote && (c == '\'' || c == '"')) {
            quote = c;
            literal = 0;
            lx.plain = 0;
        }
        if (quoted && (quote || !literal || lx.end - at == 2)) memset(quoted + at, 1, lx.end - at);
        if (text && literal) {
            if (used + 1 >= capacity) *text = cmdr_realloc(*text, capacity = capacity * 2 + 16, 1);
            (*text)[used++] = c;
        }
    }
    if (!lx.problem && quote) lx.problem = "unclosed quote";
    if (text) {
        if (!*text) *text = cmdr_realloc(NULL, 1, 1);
        (*text)[used] = '\0';
    }
    return lx;
}

/* Whether p ends a parameter: whitespace, ')', or the end of the text. */
static int param_end(const char *p)
{
    return !*p || isspace((unsigned char)*p) || *p == ')';
}

/* Skip whitespace, then a comment: '#' where a pipeline starts, to the end of
 * its line. */
static size_t skip_comment(const char *s, size_t i, size_t n, size_t *comment)
{
    while (i < n && isspace((unsigned char)s[i])) ++i;
    *comment = i;
    if (i < n && s[i] == '#')
        while (i < n && s[i] != '\n') ++i;
    return i;
}

/* Check the reference at s[0, n), "$name" or "$MODULE.NAME". */
static int check_reference(const char *s, size_t n, char **error)
{
    if (n == 2 && s[1] == '_') return cmdr_error(error, "invalid reference '$_': nothing is bound to _");
    /* A reference may name a module's binding, so '.' joins identifiers. */
    size_t k = 0;
    for (size_t m; (m = identifier(s + 1 + k)) && 1 + (k += m) < n && s[1 + k] == '.';) ++k;
    if (n == 1 || 1 + k != n || s[n - 1] == '.')
        return cmdr_error(error, "invalid reference '%.*s': use $name, declared as "
                          "{ (name) ... } or bound by as; quote '$' for literal text", (int)n, s);
    return 0;
}

/* Parse the whole pipeline before performing any effects. */
static int parse(const char *line, program *pg, char **error)
{
    *pg = (program){.stages = cmdr_realloc(NULL, 1, sizeof(stage)), .count = 1};
    pg->stages[0] = (stage){0};
    size_t n = strlen(line), comment, i = skip_comment(line, 0, n, &comment);
    while (i < n) {
        stage *s = &pg->stages[pg->count - 1];
        if (isspace((unsigned char)line[i])) {
            ++i;
            continue;
        }
        if (line[i] == '|') {
            if (!s->count) { cmdr_error(error, "expected a command before '|'"); goto fail; }
            pg->stages = cmdr_realloc(pg->stages, pg->count + 1, sizeof(stage));
            pg->stages[pg->count++] = (stage){0};
            ++i;
            continue;
        }
        char *text = NULL;
        lexeme lx = read_lexeme(line, i, n, &text, NULL);
        word w = {0};
        if (lx.problem) {
            free(text);
            cmdr_error(error, "%s", lx.problem);
            goto fail;
        }
        if (lx.kind == LEX_WORD) {
            w.text = text;
        } else {
            free(text);
            if (lx.kind == LEX_REF) {
                if (check_reference(line + i, lx.end - i, error)) goto fail;
                w = (word){.text = cmdr_strndup(line + i + 1, lx.end - i - 1), .ref = 1};
            } else {
                size_t open = lx.kind == LEX_SUB ? 2 : 1;
                w = (word){.text = cmdr_strndup(line + i + open, lx.close - i - open),
                           .block = lx.kind == LEX_BLOCK, .sub = lx.kind == LEX_SUB};
            }
        }
        s->words = cmdr_realloc(s->words, s->count + 1, sizeof(word));
        s->words[s->count++] = w;
        i = lx.end;
    }
    if (pg->count > 1 && !pg->stages[pg->count - 1].count) {
        cmdr_error(error, "expected a command after '|'");
        goto fail;
    }
    return 0;
fail:
    dispose(pg);
    return 1;
}

typedef int (*visitor)(const cmdr_token *, void *);

static int emit(visitor visit, void *data, cmdr_token_kind kind, size_t start, size_t end,
                int command, int plain)
{
    return visit(&(cmdr_token){kind, start, end, command, plain}, data);
}

/* Scan a pipeline in s[i, n), as parse reads it; a block's parameters are
 * read as compile_block reads them. Returns nonzero if visit stopped. */
static int scan(const char *s, size_t i, size_t n, unsigned char *quoted, visitor visit, void *data)
{
    size_t comment;
    i = skip_comment(s, i, n, &comment);
    if (i > comment && emit(visit, data, CMDR_TOKEN_COMMENT, comment, i, 0, 0)) return 1;
    int command = 1;
    while (i < n) {
        if (isspace((unsigned char)s[i])) {
            ++i;
            continue;
        }
        if (s[i] == '|') {
            if (emit(visit, data, CMDR_TOKEN_PIPE, i, i + 1, 0, 0)) return 1;
            command = 1;
            ++i;
            continue;
        }
        lexeme lx = read_lexeme(s, i, n, NULL, quoted);
        int nested = lx.kind == LEX_BLOCK || lx.kind == LEX_SUB;
        if (!nested || lx.deep) {
            cmdr_token_kind kind = lx.kind == LEX_REF ? CMDR_TOKEN_REFERENCE : CMDR_TOKEN_WORD;
            if (emit(visit, data, kind, i, lx.end, command, lx.plain && !nested)) return 1;
            command = 0;
            i = lx.end;
            continue;
        }
        int sub = lx.kind == LEX_SUB;
        size_t j = i + (sub ? 2 : 1);
        if (emit(visit, data, sub ? CMDR_TOKEN_SUB_OPEN : CMDR_TOKEN_BLOCK_OPEN, i, j, 0, 0)) return 1;
        while (!sub && j < lx.close && isspace((unsigned char)s[j])) ++j;
        if (!sub && j < lx.close && s[j] == '(') {
            if (emit(visit, data, CMDR_TOKEN_PARAMS_OPEN, j, j + 1, 0, 0)) return 1;
            for (++j; j < lx.close && s[j] != ')';) {
                if (isspace((unsigned char)s[j])) { ++j; continue; }
                size_t start = j;
                while (j < lx.close && !param_end(s + j) && s[j] != '=') ++j;
                if (emit(visit, data, CMDR_TOKEN_PARAM, start, j, 0, 1)) return 1;
                if (j < lx.close && s[j] == '=') { /* A default, an ordinary word. */
                    start = j;
                    while (j < lx.close && !param_end(s + j)) ++j;
                    if (emit(visit, data, CMDR_TOKEN_WORD, start, j, 0, 1)) return 1;
                }
            }
            if (j < lx.close) {
                if (emit(visit, data, CMDR_TOKEN_PARAMS_CLOSE, j, j + 1, 0, 0)) return 1;
                ++j;
            }
        }
        if (scan(s, j, lx.close, quoted, visit, data)) return 1;
        if (lx.close < n
            && emit(visit, data, sub ? CMDR_TOKEN_SUB_CLOSE : CMDR_TOKEN_BLOCK_CLOSE, lx.close,
                    lx.close + 1, 0, 0))
            return 1;
        command = 0;
        i = lx.end;
    }
    return 0;
}

void cmdr_scan(const char *text, size_t n, unsigned char *quoted,
               int (*visit)(const cmdr_token *, void *), void *data)
{
    if (quoted) memset(quoted, 0, n);
    scan(text, 0, n, quoted, visit, data);
}

const cmdr_command *cmdr_find(const cmdr *repl, const char *name)
{
    for (; repl; repl = repl->parent)
        for (size_t i = 0; i < repl->count; ++i)
            if (!strcmp(repl->commands[i].name, name)) return &repl->commands[i];
    return NULL;
}

static int value_stage(const stage *s)
{
    return s->count == 1 && s->words[0].ref;
}

/* Names visible to a body, innermost last; a name's index is its slot. */
typedef struct {
    char **names;
    int *used;
    size_t count;
} scope;

static int compile_body(const cmdr *repl, const char *line, scope *sc, program *pg,
                        char **error);

/* Length of a default at p: "" or '' for empty text, else a bare word. */
static size_t default_length(const char *p, int *empty)
{
    *empty = (p[0] == '"' || p[0] == '\'') && p[1] == p[0];
    if (*empty) return 2;
    size_t n = 0;
    while (p[n] && !isspace((unsigned char)p[n]) && !strchr("'\"\\|${})", p[n])) ++n;
    return n;
}

/* Compile block source: "(a b=1)" parameters, appended to sc while the body
 * compiles, then a pipeline. Defaults are literal and trail the required
 * parameters; the first parameter has none. See docs/dev/defaults.md. */
static int compile_block(const cmdr *repl, const char *text, scope *sc, cmdr_code **out,
                         char **error)
{
    cmdr_code *code = cmdr_realloc(NULL, 1, sizeof(*code));
    *code = (cmdr_code){.refs = 1, .scope = repl};
    size_t outer = sc->count;
    const char *p = text;
    int status = 0;
    while (isspace((unsigned char)*p)) ++p;
    if (*p == '(') {
        code->declared = 1;
        for (++p;;) {
            while (isspace((unsigned char)*p)) ++p;
            if (*p == ')') { ++p; break; }
            /* _ takes an argument the block ignores: exempt from the unused
             * check, and never referenced, so it may repeat. */
            if (*p == '_' && (p[1] == '=' || param_end(p + 1))) {
                if (p[1] == '=') {
                    status = cmdr_error(error, "parameter _ cannot have a default");
                    break;
                }
                if (code->required < code->params) {
                    status = cmdr_error(error, "parameter _ has no default but follows one that does");
                    break;
                }
                sc->names = cmdr_realloc(sc->names, sc->count + 1, sizeof(*sc->names));
                sc->used = cmdr_realloc(sc->used, sc->count + 1, sizeof(*sc->used));
                sc->used[sc->count] = 1;
                sc->names[sc->count++] = cmdr_strndup("_", 1);
                ++code->required;
                ++code->params;
                ++p;
                continue;
            }
            size_t n = identifier(p), vn = 0;
            int has_default = n && p[n] == '=', empty = 0;
            if (has_default) vn = default_length(p + n + 1, &empty);
            const char *end = has_default ? p + n + 1 + vn : p + n;
            if (!n || (!has_default && !param_end(end))) {
                status = cmdr_error(error, "invalid parameter list in '{%s}'", text);
                break;
            }
            if (has_default && (!vn || !param_end(end))) {
                status = cmdr_error(error, "invalid default for $%.*s in '{%s}': use a word without "
                                    "spaces, quotes, or ')', or \"\" for empty text", (int)n, p, text);
                break;
            }
            /* A first parameter with a default would let a block run without
             * its input, the silent failure exact arity prevents. */
            if (has_default && !code->params) {
                status = cmdr_error(error, "parameter $%.*s: the first parameter cannot have a default",
                                    (int)n, p);
                break;
            }
            if (!has_default && code->required < code->params) {
                status = cmdr_error(error, "parameter $%.*s has no default but follows one that does",
                                    (int)n, p);
                break;
            }
            sc->names = cmdr_realloc(sc->names, sc->count + 1, sizeof(*sc->names));
            sc->used = cmdr_realloc(sc->used, sc->count + 1, sizeof(*sc->used));
            sc->used[sc->count] = 0;
            sc->names[sc->count++] = cmdr_strndup(p, n);
            if (has_default) {
                size_t k = code->params - code->required;
                code->defaults = cmdr_realloc(code->defaults, k + 1, sizeof(*code->defaults));
                code->defaults[k] = empty ? cmdr_strndup("", 0) : cmdr_strndup(p + n + 1, vn);
            } else {
                ++code->required;
            }
            ++code->params;
            p = end;
        }
    }
    if (!status) status = compile_body(repl, p, sc, &code->body, error);
    /* An unused parameter would never receive its argument, since only
     * { ... } pipes its input. */
    for (size_t i = outer; !status && i < sc->count; ++i)
        if (!sc->used[i]) {
            status = cmdr_error(error, "parameter $%s is never used", sc->names[i]);
            dispose(&code->body);
        }
    while (sc->count > outer) free(sc->names[--sc->count]);
    if (status) {
        free_defaults(code);
        free(code);
    } else {
        *out = code;
    }
    return status;
}

/* Parse line, resolve references to slots, compile nested blocks, and check
 * command names, so errors precede any effects. */
static int compile_body(const cmdr *repl, const char *line, scope *sc, program *pg,
                        char **error)
{
    if (parse(line, pg, error)) return 1;
    for (size_t i = 0; i < pg->count; ++i) {
        stage *s = &pg->stages[i];
        for (size_t j = 0; j < s->count; ++j) {
            word *w = &s->words[j];
            size_t k = sc->count;
            while (w->ref && k && strcmp(sc->names[k - 1], w->text)) --k;
            if (w->ref && !k) {
                /* No block declares it: a binding, resolved where the line compiles. */
                const cmdr_command *c = cmdr_find(repl, w->text);
                if (c && c->value) {
                    w->constant = c->value;
                } else {
                    if (c) cmdr_error(error, "invalid reference '$%s': %s is a command, not a "
                                      "value; call it as a stage", w->text, w->text);
                    else cmdr_error(error, "invalid reference '$%s': no enclosing block "
                                    "declares it, and no binding names it", w->text);
                    goto fail;
                }
            } else if (w->ref) {
                sc->used[w->slot = k - 1] = 1;
            }
            if (w->block && compile_block(repl, w->text, sc, &w->code, error)) goto fail;
            if (w->sub) {
                /* In the enclosing scope, so $name means what it does beside it. */
                const char *t = w->text;
                while (isspace((unsigned char)*t)) ++t;
                if (!*t) {
                    cmdr_error(error, "empty sub-pipeline $()");
                    goto fail;
                }
                w->pipeline = cmdr_realloc(NULL, 1, sizeof(*w->pipeline));
                if (compile_body(repl, w->text, sc, w->pipeline, error)) {
                    free(w->pipeline);
                    w->pipeline = NULL;
                    goto fail;
                }
            }
        }
        if (!s->count || value_stage(s)) continue;
        if (s->words[0].ref || s->words[0].block || s->words[0].sub) {
            cmdr_error(error, "command name cannot be a %s", s->words[0].ref ? "reference"
                       : s->words[0].block ? "block" : "sub-pipeline; write the pipeline itself");
            goto fail;
        }
        if (!cmdr_find(repl, s->words[0].text)) {
            cmdr_error(error, "unknown command: %s", s->words[0].text);
            goto fail;
        }
    }
    return 0;
fail:
    dispose(pg);
    return 1;
}

/* A mismatch from a def called on a line names the call and its arguments,
 * in place of the def's name alone. */
static void frame(const cmdr *repl, const char *name, size_t argc, const cmdr_value *argv,
                  char **error)
{
    const cmdr_command *command = cmdr_find(repl, name);
    if (!command || !command->block || !*error) return;
    size_t n = strlen(name);
    const char *inner = *error;
    if (!strncmp(inner, name, n) && !strncmp(inner + n, ": ", 2)) inner += n + 2;
    text_buffer b = {0};
    append(&b, name, n);
    for (size_t i = 0; i < argc; ++i) {
        char *arg = cmdr_describe(&argv[i]);
        append(&b, " ", 1);
        append(&b, arg, strlen(arg));
        free(arg);
    }
    append(&b, ": ", 2);
    append(&b, inner, strlen(inner));
    free(*error);
    *error = b.data;
}

/* Run pg with input, which may be NULL, and env, the values of its scope's
 * names. Blocks it makes capture env. name labels a defined command's errors;
 * NULL otherwise. line is set for a line's own stages. */
static int evaluate(const cmdr *repl, const program *pg, const char *name, int line,
                    const cmdr_value *input, const cmdr_value *env, size_t envc,
                    cmdr_value *result, char **error)
{
    *result = (cmdr_value){0};
    /* Each stage receives the previous stage's result first. The first
     * receives the input, unless it replaces it with a value stage. */
    int have = input && !value_stage(&pg->stages[0]);
    int status = have ? cmdr_clone(input, result, error) : 0;
    for (size_t i = 0; !status && i < pg->count && pg->stages[i].count; ++i) {
        const stage *s = &pg->stages[i];
        if (value_stage(s)) {
            cmdr_free(result);
            const word *w = &s->words[0];
            status = cmdr_clone(w->constant ? w->constant : &env[w->slot], result, error);
            have = 1;
            continue;
        }
        int piped = have;
        if (piped && result->kind == CMDR_NONE) {
            status = cmdr_error(error, "command '%s' received no pipeline value", s->words[0].text);
            break;
        }
        size_t n = s->count - 1 + (size_t)piped, at = 0;
        cmdr_value *argv = cmdr_realloc(NULL, n, sizeof(*argv));
        /* Sub-pipelines' results, owned until the call returns. */
        cmdr_value *subs = cmdr_realloc(NULL, s->count, sizeof(*subs));
        size_t nsubs = 0;
        cmdr_value next = {0};
        if (piped) argv[at++] = *result;
        for (size_t j = 1; !status && j < s->count; ++j) {
            const word *w = &s->words[j];
            if (w->sub) { /* Left to right, before the call. */
                status = evaluate(repl, w->pipeline, NULL, 0, NULL, env, envc, &subs[nsubs], error);
                if (!status && subs[nsubs].kind == CMDR_NONE)
                    status = cmdr_error(error, "$(%s) returned no value", w->text);
                if (status) break;
                argv[at++] = subs[nsubs++];
                continue;
            }
            /* A block borrows env as its captured values; cmdr_clone copies them. */
            argv[at++] = w->constant ? *w->constant : w->ref ? env[w->slot] : w->block
                ? (cmdr_value){.kind = CMDR_BLOCK, .text = w->text, .code = w->code,
                               .items = (cmdr_value *)env, .count = envc}
                : (cmdr_value){.kind = CMDR_TEXT, .text = w->text};
        }
        if (!status) {
            status = cmdr_call(repl, s->words[0].text, n, argv, &next, error);
            if (status == CMDR_MISMATCH && line) frame(repl, s->words[0].text, n, argv, error);
        }
        while (nsubs) cmdr_free(&subs[--nsubs]);
        free(subs);
        free(argv);
        cmdr_free(result);
        *result = next;
        have = 1;
        if (status && name) { /* Name the defined command, giving a call chain. */
            char *inner = *error;
            cmdr_error(error, "%s: %s", name, inner ? inner : "command failed");
            free(inner);
        }
    }
    if (status) cmdr_free(result);
    return status;
}

int cmdr_eval(const cmdr *repl, const char *line, cmdr_value *result, char **error)
{
    *error = NULL;
    *result = (cmdr_value){0};
    scope sc = {0};
    program pg;
    int failed = compile_body(repl, line, &sc, &pg, error);
    free(sc.names);
    free(sc.used); /* compile_block grew it. */
    if (failed) return 1;
    int status = evaluate(repl, &pg, NULL, 1, NULL, NULL, 0, result, error);
    dispose(&pg);
    return status;
}

/* A block without a parameter list takes exactly its piped input. One with
 * defaults takes its required parameters, then up to all of them. */
static size_t min_arity(const cmdr_code *code) { return code->declared ? code->required : 1; }
static size_t max_arity(const cmdr_code *code) { return code->declared ? code->params : 1; }

static int arity_error(const char *name, const cmdr_code *code, size_t argc, char **error)
{
    size_t lo = min_arity(code), hi = max_arity(code);
    char want[64];
    if (lo == hi) snprintf(want, sizeof(want), "%zu argument%s", lo, lo == 1 ? "" : "s");
    else snprintf(want, sizeof(want), "%zu to %zu arguments", lo, hi);
    if (name) return cmdr_error(error, "command '%s' expects %s, got %zu", name, want, argc);
    return cmdr_error(error, "block takes %s, got %zu", want, argc);
}

int cmdr_block_arity(const cmdr_value *block, size_t argc, char **error)
{
    const cmdr_code *code = block->code;
    return argc >= min_arity(code) && argc <= max_arity(code) ? 0
        : arity_error(NULL, code, argc, error);
}

/* Bind argv to the block's parameters, after its captured values. */
static int call_block(const cmdr_value *block, const char *name,
                      size_t argc, const cmdr_value *argv, cmdr_value *result, char **error)
{
    *result = (cmdr_value){0};
    const cmdr_code *code = block->code;
    if (argc < min_arity(code) || argc > max_arity(code))
        return arity_error(name, code, argc, error);
    /* Declared parameters appear only where named; only { ... } pipes its input. */
    const cmdr_value *env = block->items, *input = code->declared ? NULL : argv;
    cmdr_value *bound = NULL;
    if (code->params) {
        /* memcpy needs a non-NULL pointer even for zero bytes. */
        bound = cmdr_realloc(NULL, block->count + code->params, sizeof(*bound));
        if (block->count) memcpy(bound, block->items, block->count * sizeof(*bound));
        memcpy(bound + block->count, argv, argc * sizeof(*bound));
        /* argc >= required, so each omitted parameter has a default. The
         * default is borrowed from code, which outlives the call. */
        for (size_t i = argc; i < code->params; ++i)
            bound[block->count + i] = (cmdr_value){.kind = CMDR_TEXT,
                                                   .text = code->defaults[i - code->required]};
        env = bound;
    }
    int status = evaluate(code->scope, &code->body, name, 0, input, env, block->count + code->params,
                          result, error);
    free(bound);
    return status;
}

int cmdr_run(const cmdr_value *block, size_t argc, const cmdr_value *argv,
             cmdr_value *result, char **error)
{
    *error = NULL;
    return call_block(block, NULL, argc, argv, result, error);
}

int cmdr_call(const cmdr *repl, const char *name, size_t argc,
              const cmdr_value *argv, cmdr_value *result, char **error)
{
    /* Per thread, since it bounds this thread's stack. */
    static _Thread_local unsigned depth;
    *result = (cmdr_value){0};
    *error = NULL;
    if (interrupted) return cmdr_error(error, "interrupted");
    const cmdr_command *command = cmdr_find(repl, name);
    if (!command) return cmdr_error(error, "unknown command: %s", name);
    for (size_t i = 0; command->text && i < argc; ++i)
        if (argv[i].kind == CMDR_LIST)
            return cmdr_error(error, "command '%s' expects text; use map or join for lists", name);
        else if (argv[i].kind != CMDR_TEXT)
            return cmdr_error(error, "command '%s' expects text, got a block", name);
    if (depth >= MAX_CALL_DEPTH)
        return cmdr_error(error, "command '%s' is nested too deeply "
                          "(possible cycle in composed commands)", name);
    if (command->value)
        return argc ? cmdr_error(error, "%s takes no arguments", name)
                    : cmdr_clone(command->value, result, error);
    ++depth;
    int status = command->run ? command->run(repl, command->user, argc, argv, result, error)
        : call_block(command->block, name, argc, argv, result, error);
    --depth;
    if (status) cmdr_free(result);
    return status;
}
