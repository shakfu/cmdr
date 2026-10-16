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
    size_t capacity;
} stage;

int cmdr_error(char **error, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    char *text = NULL;
    if (n >= 0) {
        text = malloc((size_t)n + 1);
        if (text) vsnprintf(text, (size_t)n + 1, format, args);
    }
    va_end(args);
    if (!text) {
        /* Fall back so callers do not have to guard *error; only a genuine
         * out-of-memory condition leaves it NULL. */
        const char fallback[] = "cmdr: unspecified error";
        text = malloc(sizeof(fallback));
        if (text) memcpy(text, fallback, sizeof(fallback));
    }
    *error = text;
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
        char *word = NULL;
        size_t used = 0, wcap = 0;
        char quote = 0;
        while (*p && (quote || (!isspace((unsigned char)*p) && *p != '|'))) {
            char c = *p++;
            if (c == '\\' && quote != '\'') {
                if (!*p) {
                    free(word);
                    cmdr_error(error, "unfinished escape");
                    goto fail;
                }
                c = *p++;
            } else if (quote && c == quote) {
                quote = 0;
                continue;
            } else if (!quote && (c == '\'' || c == '"')) {
                quote = c;
                continue;
            }
            /* Grow the fragment buffer on demand so short words allocate only
             * a few bytes and parsing stays linear in the input; capacity
             * keeps one spare byte so the NUL terminator always fits. */
            if (used + 1 >= wcap) {
                size_t next = wcap ? wcap * 2 : 16;
                if (next > length + 1) next = length + 1;
                if (next <= wcap) next = wcap + 1; /* overflow safety */
                char *grown = realloc(word, next);
                if (!grown) { free(word); goto oom; }
                word = grown;
                wcap = next;
            }
            word[used++] = c;
        }
        if (quote) {
            free(word);
            cmdr_error(error, "unclosed quote");
            goto fail;
        }
        if (!word) word = malloc(1);
        if (!word) goto oom;
        word[used] = '\0';
        stage *s = &stages[n - 1];
        if (s->count == s->capacity) {
            size_t capacity = s->capacity ? s->capacity * 2 : 8;
            if (capacity <= s->capacity) capacity = s->count + 1; /* overflow */
            char **words = realloc(s->words, capacity * sizeof(*words));
            if (!words) { free(word); goto oom; }
            s->words = words;
            s->capacity = capacity;
        }
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

const cmdr_command *cmdr_find(const cmdr *repl, const char *name)
{
    for (; repl; repl = repl->parent)
        for (size_t i = 0; i < repl->count; ++i)
            if (!strcmp(repl->commands[i].name, name)) return &repl->commands[i];
    return NULL;
}

int cmdr_call(const cmdr *repl, const char *name, size_t argc,
              const cmdr_value *argv, cmdr_value *result, char **error)
{
    *result = (cmdr_value){0};
    *error = NULL;
    const cmdr_command *command = cmdr_find(repl, name);
    if (!command) return cmdr_error(error, "unknown command: %s", name);
    if (argc && argv[0].kind == CMDR_FAILURE && !command->handles_failure)
        return cmdr_value_clone(&argv[0], result, error);
    if (repl->depth >= CMDR_MAX_COMPOSITION_DEPTH)
        return cmdr_error(error, "command '%s' is nested too deeply "
                          "(possible cycle in composed commands)", name);
    cmdr recurse = *repl;
    recurse.depth = repl->depth + 1;
    repl = &recurse;
    int implementations = (command->run != NULL) + (command->run_value != NULL)
                        + (command->pipeline != NULL);
    if (implementations > 1)
        return cmdr_error(error, "command '%s' has multiple implementations", name);
    if (command->pipeline) {
        if (!command->pipeline[0])
            return cmdr_error(error, "command '%s' has an empty pipeline", name);
        if (argc > 1)
            return cmdr_error(error, "pipeline command '%s' accepts at most one value",
                              name);
        return cmdr_eval_value(repl, command->pipeline, argc ? &argv[0] : NULL,
                               result, error);
    }
    if (command->run_value)
        return command->run_value(repl, command->user, argc, argv, result, error);
    if (!command->run) return cmdr_error(error, "command '%s' has no implementation", name);
    const char **words = calloc(argc + 1, sizeof(*words));
    if (!words) return cmdr_error(error, "out of memory");
    for (size_t i = 0; i < argc; ++i) {
        if (argv[i].kind != CMDR_TEXT) {
            free(words);
            const char *failure = cmdr_value_failure(&argv[i]);
            if (failure) return cmdr_error(error, "%s", failure);
            return cmdr_error(error,
                "command '%s' expects text; use map or join for lists", name);
        }
        words[i] = argv[i].text;
    }
    char *text = NULL;
    int status = command->run(command->user, argc, words, &text, error);
    free(words);
    if (status) free(text);
    else if (text) *result = (cmdr_value){.kind = CMDR_TEXT, .text = text};
    return status;
}

const char *cmdr_value_failure(const cmdr_value *value)
{
    /* Iterative scan so arbitrarily deep lists cannot overflow the stack;
     * sibling frames are held on a growable heap stack. */
    const cmdr_value **stack = NULL;
    size_t cap = 0, top = 0;
    for (;;) {
        if (!value) {
            if (!top) break;
            value = stack[--top];
            continue;
        }
        if (value->kind == CMDR_FAILURE) {
            free(stack);
            return value->text ? value->text : "captured failure";
        }
        if (value->kind == CMDR_LIST && value->count) {
            for (size_t i = 1; i < value->count; ++i) {
                if (top == cap) {
                    size_t next = cap ? cap * 2 : 16;
                    const cmdr_value **grown = realloc(stack, next * sizeof(*grown));
                    if (!grown) {
                        /* Conservatively report a failure rather than risk
                         * silently discarding one on an out-of-memory scan. */
                        free(stack);
                        return "out of memory scanning values";
                    }
                    stack = grown;
                    cap = next;
                }
                stack[top++] = &value->items[i];
            }
            value = &value->items[0];
            continue;
        }
        value = NULL; /* leaf; pop the next pending sibling */
    }
    free(stack);
    return NULL;
}

int cmdr_eval_value(const cmdr *repl, const char *line, const cmdr_value *input,
                    cmdr_value *result, char **error)
{
    *result = (cmdr_value){0};
    *error = NULL;
    if (repl->depth >= CMDR_MAX_COMPOSITION_DEPTH)
        return cmdr_error(error, "pipeline nesting is too deep "
                          "(possible cycle in composed commands)");
    cmdr recurse = *repl;
    recurse.depth = repl->depth + 1;
    repl = &recurse;
    stage *stages = NULL;
    size_t count = 0;
    if (parse(line, &stages, &count, error)) return 1;
    int status = 0;
    for (size_t i = 0; i < count; ++i) {
        if (stages[i].count && !cmdr_find(repl, stages[i].words[0])) {
            status = cmdr_error(error, "unknown command: %s", stages[i].words[0]);
            goto done;
        }
    }
    if (input && cmdr_value_clone(input, result, error)) { status = 1; goto done; }
    for (size_t i = 0; i < count && stages[i].count; ++i) {
        stage *s = &stages[i];
        int piped = i != 0 || input != NULL;
        if (piped && result->kind == CMDR_NONE) {
            status = cmdr_error(error, "command '%s' received no pipeline value", s->words[0]);
            goto done;
        }
        size_t argc = s->count - 1 + piped;
        cmdr_value *argv = calloc(argc + 1, sizeof(*argv));
        if (!argv) { status = cmdr_error(error, "out of memory"); goto done; }
        size_t at = 0;
        if (piped) argv[at++] = *result;
        for (size_t j = 1; j < s->count; ++j)
            argv[at++] = (cmdr_value){.kind = CMDR_TEXT, .text = s->words[j]};
        cmdr_value next = {0};
        status = cmdr_call(repl, s->words[0], argc, argv, &next, error);
        free(argv);
        cmdr_value_free(result);
        *result = next;
        if (status) goto done;
    }
    const char *failure = cmdr_value_failure(result);
    if (failure) status = cmdr_error(error, "%s", failure);
done:
    dispose(stages, count);
    if (status) cmdr_value_free(result);
    return status;
}

int cmdr_eval(cmdr *repl, const char *line, char **result, char **error)
{
    *result = NULL;
    cmdr_value value = {0};
    int status = cmdr_eval_value(repl, line, NULL, &value, error);
    if (!status) status = cmdr_value_render(&value, result, error);
    cmdr_value_free(&value);
    return status;
}
