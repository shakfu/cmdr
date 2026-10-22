/* POSIX extended regular expressions, matched bytewise, as matches does. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE /* REG_ENHANCED */
#include "cmdr.h"

#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* macOS has the word anchors \< \> \b \B only with REG_ENHANCED; glibc always. */
#ifdef REG_ENHANCED
#define FLAGS (REG_EXTENDED | REG_ENHANCED)
#else
#define FLAGS REG_EXTENDED
#endif

static int compile(regex_t *re, const char *pattern, const char *name, char **error)
{
    int rc = regcomp(re, pattern, FLAGS);
    if (!rc) return 0;
    char message[128];
    regerror(rc, re, message, sizeof(message));
    return cmdr_error(error, "%s: invalid regex '%s': %s", name, pattern, message);
}

static int no_match(const char *name, const cmdr_value *text, const cmdr_value *pattern,
                    char **error)
{
    char *a = cmdr_describe(text), *b = cmdr_describe(pattern);
    cmdr_error(error, "%s: %s does not match %s", name, a, b);
    free(a);
    free(b);
    return CMDR_MISMATCH;
}

/* The first match and its groups: one item for find, all for groups. */
static int first(int groups, const char *name, size_t argc, const cmdr_value *argv,
                 cmdr_value *result, char **error)
{
    if (argc != 2) return cmdr_error(error, "usage: %s TEXT REGEX", name);
    regex_t re;
    if (compile(&re, argv[1].text, name, error)) return 1;
    size_t n = re.re_nsub + 1;
    regmatch_t *m = cmdr_realloc(NULL, n, sizeof(*m));
    int rc = regexec(&re, argv[0].text, n, m, 0);
    regfree(&re);
    int status = 0;
    if (rc == REG_NOMATCH) status = no_match(name, &argv[0], &argv[1], error);
    else if (rc) status = cmdr_error(error, "%s: regexec failed", name);
    else if (!groups) *result = cmdr_text(argv[0].text + m[0].rm_so, (size_t)(m[0].rm_eo - m[0].rm_so));
    else {
        *result = (cmdr_value){.kind = CMDR_LIST};
        for (size_t i = 0; i < n; ++i) /* An unmatched group is empty text. */
            cmdr_push(result, m[i].rm_so < 0 ? cmdr_text("", 0)
                      : cmdr_text(argv[0].text + m[i].rm_so, (size_t)(m[i].rm_eo - m[i].rm_so)));
    }
    free(m);
    return status;
}
COMMAND(find_command) { return first(0, "re.find", argc, argv, result, error); }
COMMAND(groups_command) { return first(1, "re.groups", argc, argv, result, error); }

/* Fail unless replacement uses only \0 to \9, up to the groups in re, and \\. */
static int check_replacement(const char *replacement, size_t groups, char **error)
{
    for (const char *p = replacement; *p; ++p) {
        if (*p != '\\') continue;
        ++p;
        if (*p == '\\') continue;
        if (*p < '0' || *p > '9')
            return cmdr_error(error, "re.replace: use \\0 to \\9 or \\\\ in REPLACEMENT");
        if ((size_t)(*p - '0') > groups)
            return cmdr_error(error, "re.replace: \\%c refers past %zu group%s", *p, groups,
                              groups == 1 ? "" : "s");
    }
    return 0;
}

static void expand(FILE *out, const char *replacement, const char *base, const regmatch_t *m)
{
    for (const char *p = replacement; *p; ++p)
        if (*p != '\\') fputc(*p, out);
        else if (*++p == '\\') fputc('\\', out);
        else if (m[*p - '0'].rm_so >= 0)
            fwrite(base + m[*p - '0'].rm_so, 1, (size_t)(m[*p - '0'].rm_eo - m[*p - '0'].rm_so), out);
}

/* ".(PATTERN)", with backreferences renumbered past the new group, or NULL if
 * PATTERN uses \9. A search after the first starts a byte early and "."
 * consumes it, so \< and \b see that byte. REG_STARTEND cannot do this on
 * macOS, which treats rm_so as the start of the string. */
static char *wrap(const char *pattern)
{
    char *w = cmdr_realloc(NULL, strlen(pattern) + 4, 1), *q = w;
    *q++ = '.';
    *q++ = '(';
    for (const char *p = pattern; *p;) {
        if (*p == '[') { /* A bracket expression, which has no escapes. */
            const char *e = p + 1;
            if (*e == '^') ++e;
            if (*e == ']') ++e;
            while (*e && *e != ']')
                if (*e == '[' && e[1] && strchr(":.=", e[1])) {
                    char d = e[1];
                    for (e += 2; *e && !(*e == d && e[1] == ']'); ++e) continue;
                    if (*e) e += 2;
                } else {
                    ++e;
                }
            if (*e) ++e;
            memcpy(q, p, (size_t)(e - p));
            q += e - p;
            p = e;
        } else if (*p == '\\' && p[1] >= '1' && p[1] <= '9') {
            if (p[1] == '9') { free(w); return NULL; }
            *q++ = '\\';
            *q++ = (char)(p[1] + 1);
            p += 2;
        } else {
            if (*p == '\\' && p[1]) *q++ = *p++;
            *q++ = *p++;
        }
    }
    *q++ = ')';
    *q = '\0';
    return w;
}

enum mode { ALL, SPLIT, REPLACE };

/* Visit matches left to right. ALL and SPLIT skip empty matches; REPLACE
 * replaces those not touching the previous match, as sed's s///g does. After
 * an empty match the next search starts one byte later, so x* terminates. */
static int scan(enum mode mode, const char *name, const char *text, const char *pattern,
                const char *replacement, cmdr_value *result, char **error)
{
    regex_t re, later;
    if (compile(&re, pattern, name, error)) return 1;
    if (replacement && check_replacement(replacement, re.re_nsub, error)) {
        regfree(&re);
        return 1;
    }
    char *wrapped = wrap(pattern);
    int bad = wrapped ? compile(&later, wrapped, name, error)
                      : cmdr_error(error, "%s: \\9 in REGEX is not supported", name);
    free(wrapped);
    if (bad) {
        regfree(&re);
        return 1;
    }
    regmatch_t m[10], w[11];
    size_t n = re.re_nsub < 9 ? re.re_nsub + 1 : 10;
    char *out_text = NULL;
    size_t out_size = 0;
    FILE *out = NULL;
    if (mode == REPLACE && !(out = open_memstream(&out_text, &out_size))) {
        regfree(&re);
        regfree(&later);
        return cmdr_error(error, "%s: %s", name, strerror(errno));
    }
    if (mode != REPLACE) *result = (cmdr_value){.kind = CMDR_LIST};
    const char *p = text, *piece = text, *previous = NULL;
    int rc;
    for (;;) {
        const char *base = p == text ? text : p - 1;
        if (p == text) rc = regexec(&re, text, n, m, 0);
        else if (!(rc = regexec(&later, base, n + 1, w, 0))) memcpy(m, w + 1, n * sizeof(*m));
        if (rc) break;
        const char *start = base + m[0].rm_so, *stop = base + m[0].rm_eo;
        if (start != stop || (mode == REPLACE && start != previous)) {
            if (mode == ALL) cmdr_push(result, cmdr_text(start, (size_t)(stop - start)));
            else if (mode == SPLIT) cmdr_push(result, cmdr_text(piece, (size_t)(start - piece)));
            else {
                fwrite(piece, 1, (size_t)(start - piece), out);
                expand(out, replacement, base, m);
            }
            piece = previous = stop;
        }
        if (!*stop) break;
        p = stop == start ? stop + 1 : stop;
    }
    regfree(&re);
    regfree(&later);
    if (rc && rc != REG_NOMATCH) {
        if (out) { fclose(out); free(out_text); }
        else cmdr_free(result);
        return cmdr_error(error, "%s: regexec failed", name);
    }
    if (mode == SPLIT && *text) /* Like split, empty text is an empty list. */
        cmdr_push(result, cmdr_text(piece, strlen(piece)));
    if (mode == REPLACE) {
        fputs(piece, out);
        fclose(out);
        *result = (cmdr_value){.kind = CMDR_TEXT, .text = out_text};
    }
    return 0;
}

COMMAND(all_command)
{
    if (argc != 2) return cmdr_error(error, "usage: re.all TEXT REGEX");
    return scan(ALL, "re.all", argv[0].text, argv[1].text, NULL, result, error);
}

COMMAND(split_command)
{
    if (argc != 2) return cmdr_error(error, "usage: re.split TEXT REGEX");
    return scan(SPLIT, "re.split", argv[0].text, argv[1].text, NULL, result, error);
}

COMMAND(replace_command)
{
    if (argc != 3) return cmdr_error(error, "usage: re.replace TEXT REGEX REPLACEMENT");
    return scan(REPLACE, "re.replace", argv[0].text, argv[1].text, argv[2].text, result, error);
}

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    TEXT("find", "find TEXT REGEX", "Return the first match", find_command),
    TEXT("groups", "groups TEXT REGEX", "List the first match and its groups", groups_command),
    TEXT("all", "all TEXT REGEX", "List all nonempty matches", all_command),
    TEXT("split", "split TEXT REGEX", "Split at nonempty matches", split_command),
    TEXT("replace", "replace TEXT REGEX REPLACEMENT",
         "Replace all matches; \\1 inserts group 1", replace_command),
};
const cmdr cmdr_ext_re = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
