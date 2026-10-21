/* POSIX extended regular expressions, matched bytewise, as matches does. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

static int compile(regex_t *re, const char *pattern, const char *name, char **error)
{
    int rc = regcomp(re, pattern, REG_EXTENDED);
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

enum mode { ALL, SPLIT, REPLACE };

/* Visit matches left to right. ALL and SPLIT skip empty matches; REPLACE
 * replaces those not touching the previous match, as sed's s///g does. After
 * an empty match the next search starts one byte later, so x* terminates. */
static int scan(enum mode mode, const char *name, const char *text, const char *pattern,
                const char *replacement, cmdr_value *result, char **error)
{
    regex_t re;
    if (compile(&re, pattern, name, error)) return 1;
    if (replacement && check_replacement(replacement, re.re_nsub, error)) {
        regfree(&re);
        return 1;
    }
    regmatch_t m[10];
    size_t n = re.re_nsub < 9 ? re.re_nsub + 1 : 10;
    char *out_text = NULL;
    size_t out_size = 0;
    FILE *out = NULL;
    if (mode == REPLACE && !(out = open_memstream(&out_text, &out_size))) {
        regfree(&re);
        return cmdr_error(error, "%s: %s", name, strerror(errno));
    }
    if (mode != REPLACE) *result = (cmdr_value){.kind = CMDR_LIST};
    const char *p = text, *piece = text, *previous = NULL;
    int flags = 0, rc;
#ifdef REG_STARTEND
    regoff_t length = (regoff_t)strlen(text);
#endif
    for (;;) {
        const char *base = p;
#ifdef REG_STARTEND /* Search all of text from p, so \< and \b see the byte before p. */
        base = text;
        m[0].rm_so = (regoff_t)(p - text);
        m[0].rm_eo = length;
        flags |= REG_STARTEND;
#endif
        if ((rc = regexec(&re, base, n, m, flags))) break;
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
        flags = REG_NOTBOL;
    }
    regfree(&re);
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
