#define _POSIX_C_SOURCE 200809L
#include "commands.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef CMDR_LINENOISE
#include "editor.h"
#endif

/* where, if non-NULL, prefixes errors with a source location. */
static int evaluate(cmdr *repl, const char *line, const char *where)
{
    char *result, *error;
    int status = cmdr_eval(repl, line, &result, &error);
    fflush(stdout); /* Keep errors in order with earlier output. */
    if (status)
        fprintf(stderr, "cmdr: %s%s\n", where ? where : "", error ? error : "command failed");
    else if (result) {
        fputs(result, stdout);
        size_t n = strlen(result);
        if (!n || result[n - 1] != '\n') putchar('\n');
        if (ferror(stdout)) { fprintf(stderr, "cmdr: output failed\n"); status = 1; }
    }
    free(result);
    free(error);
    return status;
}

static int blank_or_comment(const char *line)
{
    while (isspace((unsigned char)*line)) ++line;
    return !*line || *line == '#';
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
static int run(cmdr *repl, FILE *in, const char *path)
{
    int interactive = !path && isatty(STDIN_FILENO);
    char *line = NULL, *pipeline = NULL, where[4096];
    size_t capacity = 0, used = 0;
    unsigned long number = 0, first = 0;
    int status = 0;
#ifdef CMDR_LINENOISE
    editor *ed = interactive ? editor_open(repl) : NULL;
#endif
    for (;;) {
        const char *prompt = used ? "...> " : "cmdr> ";
        ssize_t n;
#ifdef CMDR_LINENOISE
        if (ed) {
            n = editor_read(ed, prompt, &line, &capacity);
            if (n == -2) { used = 0; continue; }
        } else
#endif
        {
            if (interactive) { fputs(prompt, stdout); fflush(stdout); }
            n = getline(&line, &capacity, in);
        }
        if (n >= 0) {
            ++number;
            if (used && blank_or_comment(line)) continue;
            if (!used) first = number;
            char *grown = realloc(pipeline, used + (size_t)n + 1);
            if (!grown) { fprintf(stderr, "cmdr: out of memory\n"); status = 1; break; }
            pipeline = grown;
            memcpy(pipeline + used, line, (size_t)n + 1);
            used += (size_t)n;
            if (continues(pipeline, used)) continue;
        }
        if (!used) break;
        if (path) snprintf(where, sizeof(where), "%s:%lu: ", path, first);
        used = 0;
#ifdef CMDR_LINENOISE
        if (ed) editor_remember(ed, pipeline);
#endif
        if (evaluate(repl, pipeline, path ? where : NULL)) {
            status = 1;
            if (path) break;
        }
        if (n < 0) break;
    }
    if (ferror(in)) { fprintf(stderr, "cmdr: %s: %s\n", path ? path : "stdin", strerror(errno)); status = 1; }
#ifdef CMDR_LINENOISE
    editor_close(ed);
#endif
    free(line);
    free(pipeline);
    return status;
}

int main(int argc, char **argv)
{
    cmdr repl;
    cmdr_commands_init(&repl);
    if (argc == 3 && !strcmp(argv[1], "-c")) return evaluate(&repl, argv[2], NULL) ? 1 : 0;
    if (argc == 1) return run(&repl, stdin, NULL);
    if (argc != 2 || argv[1][0] == '-') {
        fprintf(stderr, "usage: cmdr [-c PIPELINE | FILE]\n");
        return 2;
    }
    FILE *file = fopen(argv[1], "r");
    if (!file) { fprintf(stderr, "cmdr: %s: %s\n", argv[1], strerror(errno)); return 1; }
    int status = run(&repl, file, argv[1]);
    fclose(file);
    return status;
}
