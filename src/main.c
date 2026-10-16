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
    /* Active commands, in lookup order, skipping shadowed ones. */
    const cmdr_command **active = NULL;
    size_t count = 0;
    int width = 0;
    for (const cmdr *group = repl; group; group = group->parent)
        for (size_t i = 0; i < group->count; ++i) {
            const cmdr_command *command = &group->commands[i];
            if (cmdr_find(repl, command->name) != command) continue;
            active = cmdr_realloc(active, count + 1, sizeof(*active));
            active[count++] = command;
            const char *usage = command->usage ? command->usage : command->name;
            if ((int)strlen(usage) > width) width = (int)strlen(usage);
        }
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    if (!out) { free(active); return cmdr_error(error, "help: %s", strerror(errno)); }
    for (size_t i = 0; i < count; ++i)
        fprintf(out, "%-*s  %s\n", width,
                active[i]->usage ? active[i]->usage : active[i]->name, active[i]->docstring);
    fputs("Compose with |; the returned value becomes the next command's first argument.\n"
          "Quote spaces and pipes; $1..$N are arguments.\n", out);
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

static int def_command(const cmdr *repl, size_t argc, const cmdr_value *argv,
                       cmdr_value *result, char **error)
{
    if (argc != 2 || argv[0].kind != CMDR_TEXT || argv[1].kind != CMDR_TEXT)
        return cmdr_error(error, "usage: def NAME PIPELINE");
    const char *name = argv[0].text, *body = argv[1].text;
    if (!valid_name(name)) return cmdr_error(error, "def: invalid name '%s'", name);
    if (!strcmp(name, "def")) return cmdr_error(error, "def: cannot redefine def");
    char *detail = NULL;
    if (cmdr_check(repl, body, CMDR_ANY_ARITY, &detail)) {
        cmdr_error(error, "def %s: %s", name, detail);
        free(detail);
        return 1;
    }
    const char *p = body;
    while (isspace((unsigned char)*p)) ++p;
    if (!*p || *p == '#') return cmdr_error(error, "def %s: empty pipeline", name);
    /* One help row per command: collapse whitespace runs in the body. */
    char *docstring = cmdr_realloc(NULL, strlen(p) + sizeof("def: "), 1), *d = docstring;
    d += sprintf(d, "def: ");
    for (; *p; ++p)
        if (!isspace((unsigned char)*p)) *d++ = *p;
        else if (p[1] && !isspace((unsigned char)p[1])) *d++ = ' ';
    *d = '\0';
    struct { cmdr layer; cmdr_command command; } *node = cmdr_realloc(NULL, 1, sizeof(*node));
    /* Lexical scope: the body sees commands as they were at definition, so
     * naming itself calls the command it shadows. */
    node->command = (cmdr_command){.name = cmdr_strndup(name, strlen(name)),
                                   .docstring = docstring,
                                   .pipeline = cmdr_strndup(body, strlen(body)),
                                   .scope = session.parent};
    node->layer = (cmdr){&node->command, 1, session.parent};
    session.parent = &node->layer;
    return 0;
}

static const cmdr_command commands[] = {
    {.name = "help", .usage = "help", .docstring = "Show commands", .run = help_command},
    {.name = "def", .usage = "def NAME PIPELINE",
     .docstring = "Define a pipeline command; it may shadow others", .run = def_command},
    /* Add application commands here; they take precedence over the prelude. */
};
static const cmdr application = {commands, sizeof(commands) / sizeof(commands[0]), &cmdr_prelude};
static cmdr session = {.parent = &application};

/* where, if non-NULL, prefixes errors with a source location. */
static int evaluate(const char *line, const char *where)
{
    cmdr_value value;
    char *error;
    int status = cmdr_eval(&session, line, 0, NULL, &value, &error);
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

int main(int argc, char **argv)
{
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
