#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef CMDR_EDITOR
#include "editor.h"
#endif

static int evaluate(cmdr_session *session, const char *line)
{
    cmdr_value value;
    char *error;
    int status = cmdr_session_eval(session, line, &value, &error);
    fflush(stdout); /* Keep errors in order with earlier output. */
    if (status) fprintf(stderr, "cmdr: %s\n", error ? error : "command failed");
    else if (cmdr_print(&value, stdout)) { fprintf(stderr, "cmdr: output failed\n"); status = 1; }
    cmdr_free(&value);
    free(error);
    return status;
}

/* Run a script, or the standard prelude when path is NULL. */
static int load(cmdr_session *session, const char *path)
{
    char *error;
    int status;
    if (!path) {
        status = cmdr_session_prelude(session, &error);
    } else {
        FILE *file = fopen(path, "r");
        if (!file) { fprintf(stderr, "cmdr: %s: %s\n", path, strerror(errno)); return 1; }
        status = cmdr_session_load(session, file, path, &error);
        fclose(file);
    }
    fflush(stdout);
    if (status) fprintf(stderr, "cmdr: %s\n", error);
    free(error);
    return status;
}

/* The whole pipeline is `quit` or `exit`, ignoring surrounding blanks. */
static int quits(const char *pipeline)
{
    char word[6];
    int end = 0;
    return sscanf(pipeline, " %5s %n", word, &end) == 1 && !pipeline[end]
        && (!strcmp(word, "quit") || !strcmp(word, "exit"));
}

/* Evaluate one pipeline per line from stdin, continuing after failures. */
static int run(cmdr_session *session, const char *banner)
{
    int interactive = isatty(STDIN_FILENO);
    char *line = NULL, *pipeline = NULL;
    size_t capacity = 0, used = 0;
    int status = 0;
    int color = 0;
#ifdef CMDR_EDITOR
    editor *ed = interactive ? editor_open(cmdr_session_commands(session)) : NULL;
    color = ed && editor_color(ed);
#endif
    if (interactive) printf(color ? "\x1b[1;36m%s\x1b[0m\n" : "%s\n", banner); /* Bold cyan. */
    for (;;) {
        const char *prompt = used ? "...> " : "cmdr> ";
        ssize_t n;
#ifdef CMDR_EDITOR
        if (ed) {
            n = editor_read(ed, editor_prompt(ed, used != 0), &line, &capacity);
            if (n == -2) { used = 0; continue; } /* Ctrl-C drops the pipeline. */
        } else
#endif
        {
            if (interactive) { fputs(prompt, stdout); fflush(stdout); }
            n = getline(&line, &capacity, stdin);
        }
        if (n >= 0 && cmdr_join(&pipeline, &used, line, (size_t)n)) continue;
        if (!used) break;
        used = 0;
        if (interactive && quits(pipeline)) break;
#ifdef CMDR_EDITOR
        if (ed) editor_remember(ed, pipeline);
#endif
        if (evaluate(session, pipeline)) status = 1;
        if (n < 0) break;
    }
    if (ferror(stdin)) {
        fprintf(stderr, "cmdr: stdin: %s\n", strerror(errno));
        status = 1;
    }
#ifdef CMDR_EDITOR
    editor_close(ed);
#endif
    free(line);
    free(pipeline);
    return status;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        printf("cmdr %s\n", CMDR_VERSION);
        return 0;
    }
    /* --user replaces the standard prelude with ~/.cmdr/prelude.cmdr. */
    int user = argc > 1 && !strcmp(argv[1], "--user");
    argc -= user;
    argv += user;
    if (argc > 3 || (argc == 3 && strcmp(argv[1], "-c")) || (argc == 2 && argv[1][0] == '-')) {
        fprintf(stderr, "usage: cmdr [--user] [-c PIPELINE | FILE]\n       cmdr --version\n");
        return 2;
    }
    char *path = NULL;
    if (user) {
        const char *home = getenv("HOME");
        if (!home || !*home) { fprintf(stderr, "cmdr: --user: HOME is not set\n"); return 1; }
        path = cmdr_realloc(NULL, strlen(home) + sizeof("/.cmdr/prelude.cmdr"), 1);
        sprintf(path, "%s/.cmdr/prelude.cmdr", home);
    }
    cmdr_session *session = cmdr_session_open(&cmdr_primitives, 1, stdout);
    int status = load(session, path);
    if (!status) {
        if (argc == 3) {
            status = evaluate(session, argv[2]);
        } else if (argc == 1) {
            const char *prelude = path ? path : "standard";
            char *banner = cmdr_realloc(NULL, strlen(prelude) + sizeof("cmdr  (prelude: )") + strlen(CMDR_VERSION), 1);
            sprintf(banner, "cmdr %s (prelude: %s)", CMDR_VERSION, prelude);
            status = run(session, banner);
            free(banner);
        } else {
            status = load(session, argv[1]);
        }
    }
    cmdr_session_close(session);
    free(path);
    return status ? 1 : 0;
}
