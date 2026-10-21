#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "extensions.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
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

/* Run a script with arguments, or the standard prelude when path is NULL. */
static int load(cmdr_session *session, const char *path, size_t argc, char **argv)
{
    char *error;
    int status;
    if (!path) {
        status = cmdr_session_prelude(session, &error);
    } else {
        FILE *file = fopen(path, "r");
        if (!file) { fprintf(stderr, "cmdr: %s: %s\n", path, strerror(errno)); return 1; }
        fcntl(fileno(file), F_SETFD, FD_CLOEXEC); /* Programs it runs do not inherit it. */
        status = cmdr_session_load_args(session, file, path, argc, (const char *const *)argv,
                                        &error);
        fclose(file);
    }
    fflush(stdout);
    if (status) fprintf(stderr, "cmdr: %s\n", error);
    free(error);
    return status;
}

/* Make each extension importable: its C commands as module _NAME, and each
 * of its scripts as a module named after the file, which imports _NAME.
 * Returns the names, which the session borrows until it closes. */
static char **add_extensions(cmdr_session *session)
{
    size_t n = 0, k = 0;
    while (cmdr_extensions[n].commands) ++n;
    while (cmdr_extension_scripts[k].path) ++k;
    char **names = cmdr_realloc(NULL, n + k + 1, sizeof(*names));
    for (size_t i = 0; i < n; ++i) {
        const char *origin = strrchr(cmdr_extensions[i].origin, '/') + 1; /* "ext/NAME" */
        names[i] = cmdr_realloc(NULL, strlen(origin) + 2, 1);
        sprintf(names[i], "_%s", origin);
        cmdr_session_add_module(session, names[i], cmdr_extensions[i].commands);
    }
    for (size_t i = 0; i < k; ++i) {
        const char *path = cmdr_extension_scripts[i].path, *base = strrchr(path, '/') + 1;
        names[n + i] = cmdr_strndup(base, strlen(base) - strlen(".cmdr"));
        cmdr_session_add_source(session, names[n + i], cmdr_extension_scripts[i].source, path);
    }
    names[n + k] = NULL;
    return names;
}

typedef struct {
    const char *text;
    size_t depth; /* Open blocks and sub-pipelines. */
    size_t end;   /* Past the last token outside them. */
    int split;    /* A newline outside them precedes the current token. */
} splitter;

static int split_line(const cmdr_token *t, void *data)
{
    splitter *sp = data;
    if (!sp->depth && memchr(sp->text + sp->end, '\n', t->start - sp->end)) return sp->split = 1;
    if (t->kind == CMDR_TOKEN_BLOCK_OPEN || t->kind == CMDR_TOKEN_SUB_OPEN) ++sp->depth;
    else if (t->kind == CMDR_TOKEN_BLOCK_CLOSE || t->kind == CMDR_TOKEN_SUB_CLOSE) --sp->depth;
    if (!sp->depth) sp->end = t->end;
    return 0;
}

/* Where -c's current line ends: the first newline outside quotes, blocks, and
 * sub-pipelines, so a quoted newline stays in its pipeline, as it did when -c
 * took one. */
static size_t line_length(const char *text)
{
    splitter sp = {text, 0, 0, 0};
    cmdr_scan(text, strlen(text), NULL, split_line, &sp);
    if (sp.depth && !sp.split) return strlen(text); /* An unclosed block runs to the end. */
    const char *newline = strchr(text + sp.end, '\n');
    return newline ? (size_t)(newline - text) : strlen(text);
}

/* Evaluate -c's lines in turn, as a script's, stopping at the first failure. */
static int evaluate_lines(cmdr_session *session, const char *text)
{
    char *pipeline = NULL;
    size_t used = 0;
    int status = 0;
    while (!status && *text) {
        size_t n = line_length(text);
        if (!cmdr_join(&pipeline, &used, text, n) && used) {
            used = 0;
            status = evaluate(session, pipeline);
        }
        text += n + (text[n] == '\n');
    }
    if (!status && used) status = evaluate(session, pipeline); /* Ends in a continuation. */
    free(pipeline);
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

/* SIGINT or SIGTERM: the running line fails with "interrupted", stopping its
 * program, and main exits by the first such signal after closing the session.
 * Without SA_RESTART, so the signal also ends a blocking read or wait. */
static volatile sig_atomic_t received;

static void interrupt(int number)
{
    if (!received) received = number;
    cmdr_interrupt(1);
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
            /* Ctrl-C at the prompt is a key, which drops the pipeline. */
            if (n == -2 && !cmdr_interrupted()) { used = 0; continue; }
        } else
#endif
        {
            if (interactive) { fputs(prompt, stdout); fflush(stdout); }
            n = getline(&line, &capacity, stdin);
        }
        if (cmdr_interrupted()) break;
        if (n >= 0 && cmdr_join(&pipeline, &used, line, (size_t)n)) continue;
        if (!used) break;
        used = 0;
        if (interactive && quits(pipeline)) break;
#ifdef CMDR_EDITOR
        if (ed) editor_remember(ed, pipeline);
#endif
        if (evaluate(session, pipeline)) status = 1;
        if (cmdr_interrupted()) break;
        if (n < 0) break;
    }
    if (ferror(stdin) && !cmdr_interrupted()) {
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
    int command = argc > 1 && !strcmp(argv[1], "-c");
    if ((command && argc != 3) || (!command && argc > 1 && argv[1][0] == '-')) {
        fprintf(stderr, "usage: cmdr [--user] [-c PIPELINE | FILE [ARG ...]]\n       cmdr --version\n");
        return 2;
    }
    char *path = NULL;
    if (user) {
        const char *home = getenv("HOME");
        if (!home || !*home) { fprintf(stderr, "cmdr: --user: HOME is not set\n"); return 1; }
        path = cmdr_realloc(NULL, strlen(home) + sizeof("/.cmdr/prelude.cmdr"), 1);
        sprintf(path, "%s/.cmdr/prelude.cmdr", home);
    }
    /* A shell ignores SIGINT for background jobs; keep that, as sh does.
     * SIGTERM's default would end cmdr and leave its program running. */
    struct sigaction action = {.sa_handler = interrupt}, old;
    sigemptyset(&action.sa_mask);
    for (int i = 0; i < 2; ++i) {
        int sig = i ? SIGTERM : SIGINT;
        if (!sigaction(sig, NULL, &old) && old.sa_handler != SIG_IGN) sigaction(sig, &action, NULL);
    }
    /* An ignored SIGCHLD survives exec and makes the kernel reap children,
     * so run's waitpid would fail. */
    signal(SIGCHLD, SIG_DFL);
    cmdr_session *session = cmdr_session_open(&cmdr_primitives, 1, stdout);
    char **modules = add_extensions(session);
    int status = load(session, path, 0, NULL);
    if (!status) {
        if (command) {
            status = evaluate_lines(session, argv[2]);
        } else if (argc == 1) {
            const char *prelude = path ? path : "standard";
            char *banner = cmdr_realloc(NULL, strlen(prelude) + sizeof("cmdr  (prelude: )") + strlen(CMDR_VERSION), 1);
            sprintf(banner, "cmdr %s (prelude: %s)", CMDR_VERSION, prelude);
            status = run(session, banner);
            free(banner);
        } else {
            status = load(session, argv[1], (size_t)argc - 2, argv + 2);
        }
    }
    cmdr_session_close(session);
    for (char **name = modules; *name; ++name) free(*name);
    free(modules);
    free(path);
    if (cmdr_interrupted()) { /* So a calling shell sees the interrupt and stops. */
        int sig = received ? received : SIGINT; /* An extension may set the flag. */
        fflush(stdout);
        signal(sig, SIG_DFL);
        raise(sig);
    }
    return status ? 1 : 0;
}
