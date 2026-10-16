#include "cmdr.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **words;
    size_t count;
} stage;

int cmdr_error(char **error, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (n >= 0) {
        *error = malloc((size_t)n + 1);
        if (*error) vsnprintf(*error, (size_t)n + 1, format, args);
    }
    va_end(args);
    return 1;
}

static void dispose(stage *stages, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < stages[i].count; ++j)
            free(stages[i].words[j]);
        free(stages[i].words);
    }
    free(stages);
}

/* Parse the whole pipeline before performing any effects. */
static int parse(const char *line, stage **out, size_t *count, char **error)
{
    size_t length = strlen(line);
    stage *stages = calloc(1, sizeof(*stages));
    if (!stages) return cmdr_error(error, "out of memory");
    size_t n = 1;
    const char *p = line;
    while (*p) {
        if (isspace((unsigned char)*p)) { ++p; continue; }
        if (*p == '|') {
            if (!stages[n - 1].count) {
                cmdr_error(error, "expected a command before '|'");
                goto fail;
            }
            stage *next = realloc(stages, (n + 1) * sizeof(*stages));
            if (!next) goto oom;
            stages = next;
            stages[n++] = (stage){0};
            ++p;
            continue;
        }
        char *word = malloc(length + 1);
        if (!word) goto oom;
        size_t used = 0;
        char quote = 0;
        while (*p && (quote || (!isspace((unsigned char)*p) && *p != '|'))) {
            char c = *p++;
            if (c == '\\' && quote != '\'') {
                if (!*p) {
                    free(word);
                    cmdr_error(error, "unfinished escape");
                    goto fail;
                }
                word[used++] = *p++;
            } else if (quote && c == quote) {
                quote = 0;
            } else if (!quote && (c == '\'' || c == '"')) {
                quote = c;
            } else {
                word[used++] = c;
            }
        }
        if (quote) {
            free(word);
            cmdr_error(error, "unclosed quote");
            goto fail;
        }
        word[used] = '\0';
        stage *s = &stages[n - 1];
        char **words = realloc(s->words, (s->count + 1) * sizeof(*words));
        if (!words) { free(word); goto oom; }
        s->words = words;
        s->words[s->count++] = word;
    }
    if (n > 1 && !stages[n - 1].count) {
        cmdr_error(error, "expected a command after '|'");
        goto fail;
    }
    *out = stages;
    *count = n;
    return 0;
oom:
    cmdr_error(error, "out of memory");
fail:
    dispose(stages, n);
    return 1;
}

static const cmdr_command *lookup(const cmdr *repl, const char *name)
{
    for (size_t i = 0; i < repl->count; ++i)
        if (!strcmp(repl->commands[i].name, name)) return &repl->commands[i];
    return NULL;
}

int cmdr_eval(cmdr *repl, const char *line, char **result, char **error)
{
    *result = NULL;
    *error = NULL;
    stage *stages = NULL;
    size_t count = 0;
    if (parse(line, &stages, &count, error)) return 1;
    int status = 0;
    /* Resolve all names before executing any stage. */
    for (size_t i = 0; i < count; ++i) {
        if (stages[i].count && !lookup(repl, stages[i].words[0])) {
            status = cmdr_error(error, "unknown command: %s", stages[i].words[0]);
            goto done;
        }
    }
    for (size_t i = 0; i < count && stages[i].count; ++i) {
        stage *s = &stages[i];
        const cmdr_command *command = lookup(repl, s->words[0]);
        if (i && !*result) {
            status = cmdr_error(error, "command '%s' received no pipeline value",
                                command->name);
            goto done;
        }
        size_t argc = s->count - 1 + (i != 0);
        const char **argv = calloc(argc + 1, sizeof(*argv));
        if (!argv) {
            status = cmdr_error(error, "out of memory");
            goto done;
        }
        size_t at = 0;
        if (i) argv[at++] = *result;
        for (size_t j = 1; j < s->count; ++j) argv[at++] = s->words[j];
        char *next = NULL;
        status = command->run(command->user, argc, argv, &next, error);
        free(argv);
        free(*result);
        *result = next;
        if (status) goto done;
    }
done:
    dispose(stages, count);
    if (status) { free(*result); *result = NULL; }
    return status;
}
