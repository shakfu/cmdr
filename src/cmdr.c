#include "cmdr.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **words;
    size_t *refs;  /* Per word: N for $N, else 0. */
    size_t *binds; /* Per word: k for the k-th `as` binding, else 0. */
    const cmdr_value **outer; /* Per word: an enclosing pipeline's binding. */
    size_t count;
    size_t capacity;
    size_t bind;   /* k if this stage is `as NAME` creating binding k. */
} stage;

/* binds value for a $name reference until prepare resolves it. */
#define UNRESOLVED SIZE_MAX

typedef struct {
    stage *stages;
    size_t count;
    size_t highest;  /* Highest $N referenced. */
    size_t bindings; /* Number of `as` stages. */
    const char **names; /* Borrowed binding names, in order. */
} program;

/* Bindings of an enclosing pipeline, visible to strings its stages evaluate. */
struct cmdr_frame {
    const char *const *names;
    const cmdr_value *values;
    size_t count;
    const struct cmdr_frame *parent;
};

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
        free(stages[i].refs);
        free(stages[i].binds);
        free(stages[i].outer);
    }
    free(stages);
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

static int identifier(const char *p, const char *end)
{
    if (p == end || !isalpha((unsigned char)*p)) return 0;
    for (; p < end; ++p)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return 0;
    return 1;
}

/* Parse the whole pipeline before performing any effects. */
static int parse(const char *line, stage **out, size_t *count, char **error)
{
    size_t length = strlen(line);
    stage *stages = calloc(1, sizeof(*stages));
    if (!stages) return cmdr_error(error, "out of memory");
    size_t n = 1;
    const char *p = line;
    /* A pipeline whose first nonblank character is '#' is a comment. */
    while (isspace((unsigned char)*p)) ++p;
    if (*p == '#') p += strlen(p);
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
        size_t used = 0, wcap = 0, ref = 0, bind = 0;
        char quote = 0;
        if (*p == '$') {
            /* Unquoted words starting with '$' are reserved for references. */
            const char *start = p;
            while (*p && !isspace((unsigned char)*p) && *p != '|') ++p;
            ref = reference(start + 1, p);
            if (!ref && identifier(start + 1, p)) bind = UNRESOLVED;
            if (!ref && !bind) {
                cmdr_error(error, "invalid reference '%.*s': only $1..$N and $name are "
                           "supported; quote '$' for literal text",
                           (int)(p - start), start);
                goto fail;
            }
            used = (size_t)(p - start);
            word = malloc(used + 1);
            if (!word) goto oom;
            memcpy(word, start, used);
        }
        while (*p && (quote || (!isspace((unsigned char)*p) && *p != '|'))) {
            char c = *p++;
            if (c == '\\' && quote != '\'') {
                if (!*p) {
                    free(word);
                    cmdr_error(error, "unfinished escape");
                    goto fail;
                }
                c = *p++;
                if (quote == '"' && c == 'n') c = '\n';
                else if (quote == '"' && c == 't') c = '\t';
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
            size_t *refs = realloc(s->refs, capacity * sizeof(*refs));
            if (!refs) { free(word); goto oom; }
            s->refs = refs;
            size_t *binds = realloc(s->binds, capacity * sizeof(*binds));
            if (!binds) { free(word); goto oom; }
            s->binds = binds;
            const cmdr_value **outer = realloc(s->outer, capacity * sizeof(*outer));
            if (!outer) { free(word); goto oom; }
            s->outer = outer;
            s->capacity = capacity;
        }
        s->refs[s->count] = ref;
        s->binds[s->count] = bind;
        s->outer[s->count] = NULL;
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

static int evaluate(const cmdr *repl, const char *line, const char *name,
                    size_t argc, const cmdr_value *params, cmdr_value *result,
                    char **error);

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
        recurse.frame = NULL; /* Bodies are lexically scoped. */
        cmdr scoped;
        if (command->scope) {
            scoped = *command->scope;
            scoped.depth = recurse.depth;
            scoped.frame = NULL;
            repl = &scoped;
        }
        if (!command->pipeline[0])
            return cmdr_error(error, "command '%s' has an empty pipeline", name);
        return evaluate(repl, command->pipeline, name, argc, argv, result, error);
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

static int is_reference(const stage *s, size_t j)
{
    return s->refs[j] || s->binds[j] || s->outer[j];
}

/* Resolve name in the enclosing pipelines' bindings, innermost first. */
static const cmdr_value *lookup(const struct cmdr_frame *f, const char *name)
{
    for (; f; f = f->parent)
        for (size_t k = f->count; k-- > 0;)
            if (!strcmp(f->names[k], name)) return &f->values[k];
    return NULL;
}

/* Parse line, resolve $name bindings, and check command names and arity for
 * argc arguments. name labels pipeline-command errors; NULL otherwise. */
static int prepare(const cmdr *repl, const char *line, const char *name,
                   size_t argc, program *out, char **error)
{
    program pg = {0};
    if (parse(line, &pg.stages, &pg.count, error)) return 1;
    int status = 0;
    const char **names = NULL; /* Borrowed binding names, in order. */
    for (size_t i = 0; i < pg.count; ++i) {
        stage *s = &pg.stages[i];
        if (!s->count) continue;
        /* A lone reference is a value stage; a reference cannot name a command. */
        int lone = s->count == 1 && is_reference(s, 0);
        if (!lone && is_reference(s, 0)) {
            status = cmdr_error(error, "command name cannot be a reference: %s", s->words[0]);
            goto done;
        }
        for (size_t j = 0; j < s->count; ++j) {
            if (s->refs[j] > pg.highest) pg.highest = s->refs[j];
            if (s->binds[j] != UNRESOLVED) continue;
            size_t k = 0;
            while (k < pg.bindings && strcmp(names[k], s->words[j] + 1)) ++k;
            if (k == pg.bindings) {
                s->binds[j] = 0;
                if ((s->outer[j] = lookup(repl->frame, s->words[j] + 1))) continue;
                status = cmdr_error(error, "unbound reference %s; bind it earlier "
                                    "with 'as %s'", s->words[j], s->words[j] + 1);
                goto done;
            }
            s->binds[j] = k + 1;
        }
        if (lone) continue;
        if (!strcmp(s->words[0], "as")) {
            const char *bound = s->count == 2 ? s->words[1] : "";
            if (s->count != 2 || is_reference(s, 1)
                || !identifier(bound, bound + strlen(bound))) {
                status = cmdr_error(error, "usage: as NAME (a letter, then letters, "
                                    "digits, '_' or '-')");
                goto done;
            }
            for (size_t k = 0; k < pg.bindings; ++k)
                if (!strcmp(names[k], bound)) {
                    status = cmdr_error(error, "as: $%s is already bound", bound);
                    goto done;
                }
            const char **grown = realloc(names, (pg.bindings + 1) * sizeof(*grown));
            if (!grown) { status = cmdr_error(error, "out of memory"); goto done; }
            names = grown;
            names[pg.bindings++] = bound;
            s->bind = pg.bindings;
            continue;
        }
        if (!cmdr_find(repl, s->words[0])) {
            status = cmdr_error(error, "unknown command: %s", s->words[0]);
            goto done;
        }
    }
    size_t highest = pg.highest;
    if (argc != CMDR_ANY_ARITY && (highest ? argc != highest : argc > 1)) {
        if (name && !highest)
            status = cmdr_error(error, "pipeline command '%s' accepts at most one value", name);
        else if (name)
            status = cmdr_error(error, "command '%s' expects %zu argument%s, got %zu",
                                name, highest, highest == 1 ? "" : "s", argc);
        else if (!highest)
            status = cmdr_error(error, "pipeline without references accepts at most "
                                "one value (%zu given)", argc);
        else
            status = cmdr_error(error, "reference $%zu has no argument (%zu given)",
                                highest, argc);
        goto done;
    }
    pg.names = names;
    *out = pg;
    return 0;
done:
    free(names);
    dispose(pg.stages, pg.count);
    return status;
}

int cmdr_check(const cmdr *repl, const char *line, size_t argc, char **error)
{
    *error = NULL;
    program pg;
    if (prepare(repl, line, NULL, argc, &pg, error)) return 1;
    free(pg.names);
    dispose(pg.stages, pg.count);
    return 0;
}

int cmdr_arity(const cmdr *repl, const char *line, size_t *highest, char **error)
{
    *error = NULL;
    program pg;
    if (prepare(repl, line, NULL, CMDR_ANY_ARITY, &pg, error)) return 1;
    *highest = pg.highest;
    free(pg.names);
    dispose(pg.stages, pg.count);
    return 0;
}

/* Evaluate line with params bound to $1..$N; name is as for prepare. */
static int evaluate(const cmdr *repl, const char *line, const char *name,
                    size_t argc, const cmdr_value *params, cmdr_value *result,
                    char **error)
{
    *result = (cmdr_value){0};
    *error = NULL;
    if (repl->depth >= CMDR_MAX_COMPOSITION_DEPTH)
        return cmdr_error(error, "pipeline nesting is too deep "
                          "(possible cycle in composed commands)");
    cmdr recurse = *repl;
    recurse.depth = repl->depth + 1;
    repl = &recurse;
    program pg;
    if (prepare(repl, line, name, argc, &pg, error)) return 1;
    int status = 0;
    cmdr_value *bound = calloc(pg.bindings + 1, sizeof(*bound));
    if (!bound) { status = cmdr_error(error, "out of memory"); goto done; }
    struct cmdr_frame frame = {pg.names, bound, 0, repl->frame};
    recurse.frame = &frame;
    /* The current value is $1 to start, and keeps a name while it is a
     * reference or was just bound. A stage that names the current value gets
     * it only where named; any other stage receives it as its first argument. */
    int have = argc && !(pg.stages[0].count == 1 && is_reference(&pg.stages[0], 0));
    size_t cur_ref = have, cur_bind = 0;
    if (have && cmdr_value_clone(&params[0], result, error)) { status = 1; goto done; }
    for (size_t i = 0; i < pg.count && pg.stages[i].count; ++i) {
        stage *s = &pg.stages[i];
        if (s->count == 1 && is_reference(s, 0)) {
            if (have && result->kind == CMDR_FAILURE) continue; /* Failures persist. */
            const cmdr_value *v = s->refs[0] ? &params[s->refs[0] - 1]
                                : s->binds[0] ? &bound[s->binds[0] - 1] : s->outer[0];
            cmdr_value_free(result);
            if (cmdr_value_clone(v, result, error)) { status = 1; goto done; }
            have = 1;
            cur_ref = s->refs[0];
            cur_bind = s->binds[0];
            continue;
        }
        if (s->bind) {
            if (!have || result->kind == CMDR_NONE) {
                status = cmdr_error(error, "as %s: no pipeline value to bind", s->words[1]);
                goto done;
            }
            if (cmdr_value_clone(result, &bound[s->bind - 1], error)) { status = 1; goto done; }
            frame.count = s->bind; /* Visible to later stages' strings. */
            cur_ref = 0;
            cur_bind = s->bind;
            continue;
        }
        int named = 0;
        for (size_t j = 1; j < s->count; ++j)
            if ((cur_ref && s->refs[j] == cur_ref) || (cur_bind && s->binds[j] == cur_bind))
                named = 1;
        int piped = have && !named;
        if (piped && result->kind == CMDR_NONE) {
            status = cmdr_error(error, "command '%s' received no pipeline value", s->words[0]);
            goto done;
        }
        size_t n = s->count - 1 + piped;
        cmdr_value *argv = calloc(n + 1, sizeof(*argv));
        if (!argv) { status = cmdr_error(error, "out of memory"); goto done; }
        size_t at = 0;
        if (piped) argv[at++] = *result;
        for (size_t j = 1; j < s->count; ++j)
            argv[at++] = s->refs[j] ? params[s->refs[j] - 1]
                       : s->binds[j] ? bound[s->binds[j] - 1]
                       : s->outer[j] ? *s->outer[j]
                       : (cmdr_value){.kind = CMDR_TEXT, .text = s->words[j]};
        cmdr_value next = {0};
        status = cmdr_call(repl, s->words[0], n, argv, &next, error);
        free(argv);
        cmdr_value_free(result);
        *result = next;
        have = 1;
        cur_ref = cur_bind = 0;
        if (status) {
            if (name) { /* Name the pipeline command, giving a call chain. */
                char *inner = *error;
                cmdr_error(error, "%s: %s", name, inner ? inner : "command failed");
                free(inner);
            }
            goto done;
        }
    }
    const char *failure = cmdr_value_failure(result);
    if (failure) status = cmdr_error(error, "%s", failure);
done:
    for (size_t k = 0; bound && k < pg.bindings; ++k) cmdr_value_free(&bound[k]);
    free(bound);
    free(pg.names);
    dispose(pg.stages, pg.count);
    if (status) cmdr_value_free(result);
    return status;
}

int cmdr_eval_value(const cmdr *repl, const char *line, const cmdr_value *input,
                    cmdr_value *result, char **error)
{
    return evaluate(repl, line, NULL, input != NULL, input, result, error);
}

int cmdr_eval_args(const cmdr *repl, const char *line, size_t argc,
                   const cmdr_value *argv, cmdr_value *result, char **error)
{
    return evaluate(repl, line, NULL, argc, argv, result, error);
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
