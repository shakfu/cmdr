#include "cmdr.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Recursion limits: composed commands nest at most MAX_CALL_DEPTH deep, and
 * cmdr_clone rejects values nested more than MAX_VALUE_DEPTH deep. Every
 * stage clones its input or builds flat lists, so value depth stays bounded. */
enum { MAX_CALL_DEPTH = 128, MAX_VALUE_DEPTH = 1024 };

void *cmdr_realloc(void *p, size_t count, size_t size)
{
    if (!count) count = 1;
    if (count <= SIZE_MAX / size && (p = realloc(p, count * size))) return p;
    fputs("cmdr: out of memory\n", stderr);
    exit(1);
}

char *cmdr_strndup(const char *text, size_t n)
{
    char *copy = cmdr_realloc(NULL, n + 1, 1);
    memcpy(copy, text, n);
    copy[n] = '\0';
    return copy;
}

cmdr_value cmdr_text(const char *text, size_t n)
{
    return (cmdr_value){.kind = CMDR_TEXT, .text = cmdr_strndup(text, n)};
}

void cmdr_push(cmdr_value *list, cmdr_value item)
{
    list->items = cmdr_realloc(list->items, list->count + 1, sizeof(*list->items));
    list->items[list->count++] = item;
}

void cmdr_free(cmdr_value *value)
{
    for (size_t i = 0; i < value->count; ++i) cmdr_free(&value->items[i]);
    free(value->items);
    free(value->text);
    *value = (cmdr_value){0};
}

static int clone(const cmdr_value *value, cmdr_value *out, unsigned depth, char **error)
{
    *out = (cmdr_value){.kind = value->kind};
    if (depth > MAX_VALUE_DEPTH) return cmdr_error(error, "value is nested too deeply");
    if (value->kind == CMDR_TEXT) out->text = cmdr_strndup(value->text, strlen(value->text));
    for (size_t i = 0; i < value->count; ++i) {
        cmdr_value item;
        if (clone(&value->items[i], &item, depth + 1, error)) {
            cmdr_free(out);
            return 1;
        }
        cmdr_push(out, item);
    }
    return 0;
}

int cmdr_clone(const cmdr_value *value, cmdr_value *out, char **error)
{
    return clone(value, out, 0, error);
}

void cmdr_render(const cmdr_value *value, FILE *out)
{
    if (value->kind == CMDR_TEXT) fputs(value->text, out);
    for (size_t i = 0; i < value->count; ++i) {
        cmdr_render(&value->items[i], out);
        fputc('\n', out);
    }
}

int cmdr_error(char **error, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);
    size_t size = n < 0 ? 1 : (size_t)n + 1;
    *error = cmdr_realloc(NULL, size, 1);
    va_start(args, format);
    if (vsnprintf(*error, size, format, args) < 0) **error = '\0';
    va_end(args);
    return 1;
}

typedef struct {
    char *text;
    size_t ref; /* N for an unquoted $N, else 0. */
} word;

typedef struct {
    word *words;
    size_t count;
} stage;

typedef struct {
    stage *stages;
    size_t count;
    size_t highest; /* Highest $N referenced. */
} program;

static void dispose(program *pg)
{
    for (size_t i = 0; i < pg->count; ++i) {
        for (size_t j = 0; j < pg->stages[i].count; ++j) free(pg->stages[i].words[j].text);
        free(pg->stages[i].words);
    }
    free(pg->stages);
}

/* Return N for the digits of a valid $N reference in [p, end), else 0. */
static size_t reference(const char *p, const char *end)
{
    if (p == end || *p == '0') return 0;
    size_t n = 0;
    for (; p < end; ++p) {
        if (*p < '0' || *p > '9' || n > (SIZE_MAX - (size_t)(*p - '0')) / 10) return 0;
        n = n * 10 + (size_t)(*p - '0');
    }
    return n;
}

/* Read one word at *p: quotes group, backslash escapes, unquoted $N refers. */
static int lex(const char **p, word *out, char **error)
{
    const char *s = *p;
    if (*s == '$') {
        while (*s && !isspace((unsigned char)*s) && *s != '|') ++s;
        *out = (word){cmdr_strndup(*p, (size_t)(s - *p)), reference(*p + 1, s)};
        if (!out->ref) {
            free(out->text);
            return cmdr_error(error, "invalid reference '%.*s': only $1..$N are supported; "
                              "quote '$' for literal text", (int)(s - *p), *p);
        }
        *p = s;
        return 0;
    }
    char *text = NULL, quote = 0;
    size_t used = 0, capacity = 0;
    while (*s && (quote || (!isspace((unsigned char)*s) && *s != '|'))) {
        char c = *s++;
        if (c == '\\' && quote != '\'') {
            if (!*s) { free(text); return cmdr_error(error, "unfinished escape"); }
            c = *s++;
            if (quote == '"' && c == 'n') c = '\n';
            else if (quote == '"' && c == 't') c = '\t';
        } else if (quote && c == quote) {
            quote = 0;
            continue;
        } else if (!quote && (c == '\'' || c == '"')) {
            quote = c;
            continue;
        }
        if (used + 1 >= capacity) text = cmdr_realloc(text, capacity = capacity * 2 + 16, 1);
        text[used++] = c;
    }
    if (quote) { free(text); return cmdr_error(error, "unclosed quote"); }
    if (!text) text = cmdr_realloc(NULL, 1, 1);
    text[used] = '\0';
    *out = (word){text, 0};
    *p = s;
    return 0;
}

/* Parse the whole pipeline before performing any effects. */
static int parse(const char *line, program *pg, char **error)
{
    *pg = (program){.stages = cmdr_realloc(NULL, 1, sizeof(stage)), .count = 1};
    pg->stages[0] = (stage){0};
    const char *p = line;
    while (isspace((unsigned char)*p)) ++p;
    if (*p == '#') p += strlen(p); /* A comment. */
    while (*p) {
        stage *s = &pg->stages[pg->count - 1];
        if (isspace((unsigned char)*p)) {
            ++p;
        } else if (*p == '|') {
            if (!s->count) { cmdr_error(error, "expected a command before '|'"); goto fail; }
            pg->stages = cmdr_realloc(pg->stages, pg->count + 1, sizeof(stage));
            pg->stages[pg->count++] = (stage){0};
            ++p;
        } else {
            word w;
            if (lex(&p, &w, error)) goto fail;
            s->words = cmdr_realloc(s->words, s->count + 1, sizeof(word));
            s->words[s->count++] = w;
        }
    }
    if (pg->count > 1 && !pg->stages[pg->count - 1].count) {
        cmdr_error(error, "expected a command after '|'");
        goto fail;
    }
    return 0;
fail:
    dispose(pg);
    return 1;
}

const cmdr_command *cmdr_find(const cmdr *repl, const char *name)
{
    for (; repl; repl = repl->parent)
        for (size_t i = 0; i < repl->count; ++i)
            if (!strcmp(repl->commands[i].name, name)) return &repl->commands[i];
    return NULL;
}

static int value_stage(const stage *s)
{
    return s->count == 1 && s->words[0].ref;
}

/* Parse line and check command names, and arity for argc arguments. name
 * labels a pipeline command's errors; NULL otherwise. */
static int prepare(const cmdr *repl, const char *line, const char *name,
                   size_t argc, program *pg, char **error)
{
    if (parse(line, pg, error)) return 1;
    for (size_t i = 0; i < pg->count; ++i) {
        stage *s = &pg->stages[i];
        for (size_t j = 0; j < s->count; ++j)
            if (s->words[j].ref > pg->highest) pg->highest = s->words[j].ref;
        if (!s->count || value_stage(s)) continue;
        if (s->words[0].ref) {
            cmdr_error(error, "command name cannot be a reference: %s", s->words[0].text);
            goto fail;
        }
        if (!cmdr_find(repl, s->words[0].text)) {
            cmdr_error(error, "unknown command: %s", s->words[0].text);
            goto fail;
        }
    }
    size_t n = pg->highest;
    if (argc == CMDR_ANY_ARITY || (n ? argc == n : argc <= 1)) return 0;
    if (name && !n)
        cmdr_error(error, "pipeline command '%s' accepts at most one value", name);
    else if (name)
        cmdr_error(error, "command '%s' expects %zu argument%s, got %zu",
                   name, n, n == 1 ? "" : "s", argc);
    else if (!n)
        cmdr_error(error, "pipeline without references accepts at most one value "
                   "(%zu given)", argc);
    else
        cmdr_error(error, "reference $%zu has no argument (%zu given)", n, argc);
fail:
    dispose(pg);
    return 1;
}

int cmdr_check(const cmdr *repl, const char *line, size_t argc, char **error)
{
    program pg;
    if (prepare(repl, line, NULL, argc, &pg, error)) return 1;
    dispose(&pg);
    return 0;
}

static int evaluate(const cmdr *repl, const char *line, const char *name,
                    size_t argc, const cmdr_value *params, cmdr_value *result,
                    char **error)
{
    *result = (cmdr_value){0};
    program pg;
    if (prepare(repl, line, name, argc, &pg, error)) return 1;
    /* The current value starts as $1. A stage that references the current
     * value's $N receives it there only; other stages receive it first. */
    int have = argc && !value_stage(&pg.stages[0]);
    size_t current = (size_t)have;
    int status = have ? cmdr_clone(&params[0], result, error) : 0;
    for (size_t i = 0; !status && i < pg.count && pg.stages[i].count; ++i) {
        stage *s = &pg.stages[i];
        if (value_stage(s)) {
            current = s->words[0].ref;
            cmdr_free(result);
            status = cmdr_clone(&params[current - 1], result, error);
            have = 1;
            continue;
        }
        int piped = have;
        for (size_t j = 1; j < s->count; ++j)
            if (current && s->words[j].ref == current) piped = 0;
        if (piped && result->kind == CMDR_NONE) {
            status = cmdr_error(error, "command '%s' received no pipeline value", s->words[0].text);
            break;
        }
        size_t n = s->count - 1 + (size_t)piped, at = 0;
        cmdr_value *argv = cmdr_realloc(NULL, n, sizeof(*argv));
        cmdr_value next;
        if (piped) argv[at++] = *result;
        for (size_t j = 1; j < s->count; ++j) {
            word *w = &s->words[j];
            argv[at++] = w->ref ? params[w->ref - 1] : (cmdr_value){.kind = CMDR_TEXT, .text = w->text};
        }
        status = cmdr_call(repl, s->words[0].text, n, argv, &next, error);
        free(argv);
        cmdr_free(result);
        *result = next;
        have = 1;
        current = 0;
        if (status && name) { /* Name the pipeline command, giving a call chain. */
            char *inner = *error;
            cmdr_error(error, "%s: %s", name, inner ? inner : "command failed");
            free(inner);
        }
    }
    dispose(&pg);
    if (status) cmdr_free(result);
    return status;
}

int cmdr_eval(const cmdr *repl, const char *line, size_t argc,
              const cmdr_value *argv, cmdr_value *result, char **error)
{
    *error = NULL;
    return evaluate(repl, line, NULL, argc, argv, result, error);
}

int cmdr_call(const cmdr *repl, const char *name, size_t argc,
              const cmdr_value *argv, cmdr_value *result, char **error)
{
    static unsigned depth;
    *result = (cmdr_value){0};
    *error = NULL;
    const cmdr_command *command = cmdr_find(repl, name);
    if (!command) return cmdr_error(error, "unknown command: %s", name);
    for (size_t i = 0; command->text && i < argc; ++i)
        if (argv[i].kind != CMDR_TEXT)
            return cmdr_error(error, "command '%s' expects text; use map or join for lists", name);
    if (depth >= MAX_CALL_DEPTH)
        return cmdr_error(error, "command '%s' is nested too deeply "
                          "(possible cycle in composed commands)", name);
    ++depth;
    int status = command->pipeline
        ? evaluate(command->scope ? command->scope : repl, command->pipeline, name,
                   argc, argv, result, error)
        : command->run(repl, argc, argv, result, error);
    --depth;
    if (status) cmdr_free(result);
    return status;
}
