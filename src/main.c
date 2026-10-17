#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Names resolve here first: each def pushes a one-command layer in front of
 * the application commands. Layers are never freed, since a running body may
 * still use an earlier one. */
static cmdr session;

/* Return help as text so it composes, e.g. `help | filter list`. */
static int help_command(const cmdr *repl, size_t argc, const cmdr_value *argv,
                        cmdr_value *result, char **error)
{
    if (argc) return cmdr_error(error, "usage: help");
    /* Tables in lookup order, then definitions oldest first, which reverses
     * their lookup order. Each def is a layer of one command with a block. */
    const cmdr **groups = NULL;
    size_t ngroups = 0;
    for (const cmdr *group = repl; group; group = group->parent) {
        groups = cmdr_realloc(groups, ngroups + 1, sizeof(*groups));
        groups[ngroups++] = group;
    }
    const cmdr_command **active = NULL;
    size_t count = 0;
    int width = 0;
    for (size_t pass = 0; pass < 2; ++pass)
        for (size_t k = 0; k < ngroups; ++k) {
            const cmdr *group = groups[pass ? ngroups - 1 - k : k];
            int def = group->count && group->commands[0].block;
            if (def != (int)pass) continue;
            for (size_t i = 0; i < group->count; ++i) {
                const cmdr_command *command = &group->commands[i];
                if (cmdr_find(repl, command->name) != command) continue; /* Shadowed. */
                active = cmdr_realloc(active, count + 1, sizeof(*active));
                active[count++] = command;
                const char *usage = command->usage ? command->usage : command->name;
                if ((int)strlen(usage) > width) width = (int)strlen(usage);
            }
        }
    free(groups);
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    if (!out) { free(active); return cmdr_error(error, "help: %s", strerror(errno)); }
    for (size_t i = 0; i < count; ++i)
        fprintf(out, "%-*s  %s\n", width,
                active[i]->usage ? active[i]->usage : active[i]->name, active[i]->docstring);
    fputs("Compose with |; the returned value becomes the next command's first argument.\n"
          "Quote spaces and pipes; { (a b) ... } is a block taking $a and $b.\n", out);
    fclose(out);
    free(active);
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = text};
    return 0;
}

/* A name must be callable as one unquoted word. */
static int valid_name(const char *name)
{
    if (!*name || *name == '$' || *name == '#') return 0;
    for (const char *p = name; *p; ++p)
        if (isspace((unsigned char)*p) || strchr("|'\"\\", *p)) return 0;
    return 1;
}

/* Help syntax from the block: "NAME A B" for { (a b) ... }, "NAME VALUE" for
 * { ... }, which takes one piped input. */
static char *usage_of(const char *name, const char *block)
{
    const char *p = block;
    while (isspace((unsigned char)*p)) ++p;
    size_t n = strlen(name);
    char *usage = cmdr_realloc(NULL, n + strlen(p) + sizeof(" VALUE"), 1), *u = usage + n;
    memcpy(usage, name, n);
    if (*p != '(') u += sprintf(u, " VALUE");
    else
        for (++p; *p && *p != ')'; ++p)
            if (!isspace((unsigned char)*p)) {
                if (p[-1] == '(' || isspace((unsigned char)p[-1])) *u++ = ' ';
                *u++ = (char)toupper((unsigned char)*p);
            }
    *u = '\0';
    return usage;
}

static int def_command(const cmdr *repl, size_t argc, const cmdr_value *argv,
                       cmdr_value *result, char **error)
{
    if (argc < 2 || argc > 3 || argv[0].kind != CMDR_TEXT || argv[1].kind != CMDR_BLOCK
        || (argc == 3 && argv[2].kind != CMDR_TEXT))
        return cmdr_error(error, "usage: def NAME BLOCK [DOC]");
    const char *name = argv[0].text, *body = argv[1].text;
    if (!valid_name(name)) return cmdr_error(error, "def: invalid name '%s'", name);
    if (!strcmp(name, "def")) return cmdr_error(error, "def: cannot redefine def");
    /* Its line checked the block; a block inside another captures names. */
    if (argv[1].count) return cmdr_error(error, "def %s: block uses enclosing parameters", name);
    const char *p = body;
    while (isspace((unsigned char)*p)) ++p;
    if (!*p) return cmdr_error(error, "def %s: empty block", name);
    /* One help row per command: DOC, or the body with whitespace runs collapsed. */
    char *docstring = cmdr_realloc(NULL, strlen(p) + sizeof("def: "), 1), *d = docstring;
    d += sprintf(d, "def: ");
    for (; *p; ++p)
        if (!isspace((unsigned char)*p)) *d++ = *p;
        else if (p[1] && !isspace((unsigned char)p[1])) *d++ = ' ';
    *d = '\0';
    if (argc == 3) {
        free(docstring);
        docstring = cmdr_strndup(argv[2].text, strlen(argv[2].text));
    }
    struct { cmdr layer; cmdr_command command; cmdr_value block; } *node
        = cmdr_realloc(NULL, 1, sizeof(*node));
    if (cmdr_clone(&argv[1], &node->block, error)) { free(node); free(docstring); return 1; }
    /* Lexical scope: the body sees commands as they were at definition, so
     * naming itself calls the command it shadows. */
    node->command = (cmdr_command){.name = cmdr_strndup(name, strlen(name)),
                                   .usage = usage_of(name, body),
                                   .docstring = docstring,
                                   .block = &node->block,
                                   .scope = session.parent};
    node->layer = (cmdr){&node->command, 1, session.parent};
    session.parent = &node->layer;
    return 0;
}

static int run(FILE *in, const char *path);

/* Run a script's lines in this session, so its definitions remain. */
static int load_command(const cmdr *repl, size_t argc, const cmdr_value *argv,
                        cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: load PATH");
    FILE *file = fopen(argv[0].text, "r");
    if (!file) return cmdr_error(error, "load %s: %s", argv[0].text, strerror(errno));
    int status = run(file, argv[0].text);
    fclose(file);
    /* run reported the failing line. */
    return status ? cmdr_error(error, "load %s failed", argv[0].text) : 0;
}

static const cmdr_command commands[] = {
    {.name = "help", .usage = "help", .docstring = "Show commands", .run = help_command},
    {.name = "def", .usage = "def NAME BLOCK [DOC]",
     .docstring = "Define a command; it may shadow others", .run = def_command},
    {.name = "load", .usage = "load PATH", .docstring = "Run a script in this session",
     .run = load_command, .text = 1},
    /* Add application commands here; they take precedence over the primitives. */
};
static const cmdr application = {commands, sizeof(commands) / sizeof(commands[0]), &cmdr_primitives};
static cmdr session = {.parent = &application};

/* where, if non-NULL, prefixes errors with a source location. */
static int evaluate(const char *line, const char *where)
{
    cmdr_value value;
    char *error;
    int status = cmdr_eval(&session, line, &value, &error);
    fflush(stdout); /* Keep errors in order with earlier output. */
    if (status) {
        fprintf(stderr, "cmdr: %s%s\n", where ? where : "", error ? error : "command failed");
    } else if (value.kind != CMDR_NONE) {
        cmdr_render(&value, stdout);
        size_t n = value.text ? strlen(value.text) : 0;
        if (value.kind == CMDR_LIST ? !value.count : (!n || value.text[n - 1] != '\n'))
            putchar('\n');
        if (fflush(stdout)) { fprintf(stderr, "cmdr: output failed\n"); status = 1; }
    }
    cmdr_free(&value);
    free(error);
    return status;
}

static int blank_or_comment(const char *line)
{
    while (isspace((unsigned char)*line)) ++line;
    return !*line || *line == '#';
}

/* The whole pipeline is `quit` or `exit`, ignoring surrounding blanks. */
static int quits(const char *pipeline)
{
    char word[6];
    int end = 0;
    return sscanf(pipeline, " %5s %n", word, &end) == 1 && !pipeline[end]
        && (!strcmp(word, "quit") || !strcmp(word, "exit"));
}

/* A line whose last nonblank character is an unescaped '|' continues. */
static int continues(const char *line, size_t n)
{
    while (n && isspace((unsigned char)line[n - 1])) --n;
    return n && line[n - 1] == '|' && (n < 2 || line[n - 2] != '\\')
        && !blank_or_comment(line);
}

/* Evaluate one pipeline per line, joining continued lines; comment lines
 * inside a continuation are skipped. A script (path non-NULL) stops at the
 * first failure, since later lines may depend on earlier ones; stdin
 * continues. Errors report the pipeline's first line. */
static int run(FILE *in, const char *path)
{
    int interactive = !path && isatty(STDIN_FILENO);
    char *line = NULL, *pipeline = NULL, where[4096];
    size_t capacity = 0, used = 0;
    unsigned long number = 0, first = 0;
    int status = 0;
    for (;;) {
        if (interactive) { fputs(used ? "...> " : "cmdr> ", stdout); fflush(stdout); }
        ssize_t n = getline(&line, &capacity, in);
        if (n >= 0) {
            ++number;
            if (used && blank_or_comment(line)) continue;
            if (!used) first = number;
            pipeline = cmdr_realloc(pipeline, used + (size_t)n + 1, 1);
            memcpy(pipeline + used, line, (size_t)n + 1);
            used += (size_t)n;
            if (continues(pipeline, used)) continue;
        }
        if (!used) break;
        used = 0;
        if (interactive && quits(pipeline)) break;
        if (path) snprintf(where, sizeof(where), "%s:%lu: ", path, first);
        if (evaluate(pipeline, path ? where : NULL)) {
            status = 1;
            if (path) break;
        }
        if (n < 0) break;
    }
    if (ferror(in)) {
        fprintf(stderr, "cmdr: %s: %s\n", path ? path : "stdin", strerror(errno));
        status = 1;
    }
    free(line);
    free(pipeline);
    return status;
}

/* src/prelude.cmdr, embedded by the build. */
extern const char cmdr_prelude_source[];

int main(int argc, char **argv)
{
    size_t n = strlen(cmdr_prelude_source);
    FILE *prelude = n ? fmemopen((void *)cmdr_prelude_source, n, "r") : NULL;
    if (n && !prelude) { fprintf(stderr, "cmdr: prelude: %s\n", strerror(errno)); return 1; }
    int failed = prelude && run(prelude, "prelude.cmdr");
    if (prelude) fclose(prelude);
    if (failed) return 1;
    if (argc == 3 && !strcmp(argv[1], "-c")) return evaluate(argv[2], NULL) ? 1 : 0;
    if (argc == 1) return run(stdin, NULL);
    if (argc != 2 || argv[1][0] == '-') {
        fprintf(stderr, "usage: cmdr [-c PIPELINE | FILE]\n");
        return 2;
    }
    FILE *file = fopen(argv[1], "r");
    if (!file) { fprintf(stderr, "cmdr: %s: %s\n", argv[1], strerror(errno)); return 1; }
    int status = run(file, argv[1]);
    fclose(file);
    return status;
}
