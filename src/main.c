#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "extensions.h"

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
        status = cmdr_session_load_args(session, file, path, argc, (const char *const *)argv,
                                        &error);
        fclose(file);
    }
    fflush(stdout);
    if (status) fprintf(stderr, "cmdr: %s\n", error);
    free(error);
    return status;
}

/* Each extension's commands as a layer, in front of the next and then the
 * primitives. Copies put "ext/NAME: " before docstrings, so help tells them
 * from primitives. */
static cmdr *chain_extensions(void)
{
    size_t n = 0;
    while (cmdr_extensions[n].commands) ++n;
    cmdr *layers = cmdr_realloc(NULL, n + 1, sizeof(*layers));
    for (size_t i = 0; i < n; ++i) {
        const cmdr *table = cmdr_extensions[i].commands;
        const char *origin = cmdr_extensions[i].origin;
        cmdr_command *commands = cmdr_realloc(NULL, table->count, sizeof(*commands));
        for (size_t j = 0; j < table->count; ++j) {
            const char *doc = table->commands[j].docstring ? table->commands[j].docstring : "";
            char *tagged = cmdr_realloc(NULL, strlen(origin) + strlen(doc) + sizeof(": "), 1);
            sprintf(tagged, "%s: %s", origin, doc);
            commands[j] = table->commands[j];
            commands[j].docstring = tagged;
        }
        layers[i] = (cmdr){commands, table->count, &layers[i + 1]};
    }
    layers[n] = cmdr_primitives;
    return layers;
}

static void free_extensions(cmdr *layers)
{
    for (size_t i = 0; cmdr_extensions[i].commands; ++i) {
        for (size_t j = 0; j < layers[i].count; ++j)
            free((char *)layers[i].commands[j].docstring);
        free((cmdr_command *)layers[i].commands);
    }
    free(layers);
}

/* Run the ext/ scripts before the prelude, which --user replaces, so they
 * cannot depend on it. ends[k + 1] is the newest definition after script k. */
static int load_extensions(cmdr_session *session, const cmdr ***ends)
{
    size_t n = 0;
    while (cmdr_extension_scripts[n].path) ++n;
    *ends = cmdr_realloc(NULL, n + 1, sizeof(**ends));
    (*ends)[0] = cmdr_session_commands(session)->parent;
    for (size_t k = 0; k < n; ++k) {
        char *error;
        int status = cmdr_session_source(session, cmdr_extension_scripts[k].source,
                                         cmdr_extension_scripts[k].path, &error);
        fflush(stdout);
        if (status) fprintf(stderr, "cmdr: %s\n", error);
        free(error);
        if (status) return 1;
        (*ends)[k + 1] = cmdr_session_commands(session)->parent;
    }
    return 0;
}

/* Fail on a command whose name is also in front of it or behind it, since
 * prelude definitions would bind to the wrong one. */
static int check(cmdr_session *session, const cmdr *layer, const char *origin)
{
    int status = 0;
    for (size_t i = 0; i < layer->count; ++i) {
        const cmdr_command *command = &layer->commands[i];
        if (cmdr_find(cmdr_session_commands(session), command->name) != command
            || cmdr_find(layer->parent, command->name)) {
            fprintf(stderr, "cmdr: %s: '%s' is already a command\n", origin, command->name);
            status = 1;
        }
    }
    return status;
}

/* Check the ext/ C commands, then each script's definitions, after the prelude. */
static int check_extensions(cmdr_session *session, const cmdr *layers, const cmdr **ends)
{
    int status = 0;
    for (size_t i = 0; cmdr_extensions[i].commands; ++i)
        status |= check(session, &layers[i], cmdr_extensions[i].origin);
    for (size_t k = 0; cmdr_extension_scripts[k].path; ++k)
        for (const cmdr *layer = ends[k + 1]; layer != ends[k]; layer = layer->parent)
            status |= check(session, layer, cmdr_extension_scripts[k].path);
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
    cmdr *layers = chain_extensions();
    cmdr_session *session = cmdr_session_open(layers, 1, stdout);
    const cmdr **ends = NULL;
    int status = load_extensions(session, &ends) || load(session, path, 0, NULL)
        || check_extensions(session, layers, ends);
    if (!status) {
        if (command) {
            status = evaluate(session, argv[2]);
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
    free_extensions(layers);
    free(ends);
    free(path);
    return status ? 1 : 0;
}
