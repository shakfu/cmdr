#ifndef CMDR_H
#define CMDR_H

#include <stddef.h>

/* Recursion guards. */
/* Composed commands (`.pipeline`, `map`, `try`) may nest at most this deep;
 * exceeding it is reported as an error instead of a stack overflow. */
#define CMDR_MAX_COMPOSITION_DEPTH 128
/* Values nested deeper than this cannot be cloned or rendered; freeing is
 * always iterative and has no depth limit. */
#define CMDR_MAX_VALUE_DEPTH 1024

/* Status for a predicate that does not hold, as opposed to an error (1).
 * keep and not treat only this status as false. Set *error as usual. */
#define CMDR_MISMATCH 2

typedef struct cmdr cmdr;
typedef struct cmdr_value cmdr_value;

typedef enum { CMDR_NONE, CMDR_TEXT, CMDR_LIST, CMDR_FAILURE } cmdr_kind;

/* Owned values. List items may themselves be values; text cannot contain NUL.
 * Initialize with {0}; release with cmdr_value_free(). */
struct cmdr_value {
    cmdr_kind kind;
    char *text;
    cmdr_value *items;
    size_t count;
};

void cmdr_value_free(cmdr_value *value);
int cmdr_value_text(cmdr_value *out, const char *text, char **error);
int cmdr_value_clone(const cmdr_value *value, cmdr_value *out, char **error);
/* Move item into list on success; item remains owned by caller on failure. */
int cmdr_value_push(cmdr_value *list, cmdr_value *item, char **error);
/* Render lists as newline-delimited text. Use typed values to retain boundaries.
 * Fails with an error if the value nests deeper than CMDR_MAX_VALUE_DEPTH. */
int cmdr_value_render(const cmdr_value *value, char **text, char **error);
/* Return the text of the first captured failure anywhere inside value, or NULL.
 * Iterative: safe on arbitrarily deep values. The returned pointer is borrowed. */
const char *cmdr_value_failure(const cmdr_value *value);

/* Arguments are borrowed for the duration of the call. On success, return a
 * malloc-owned, NUL-terminated string through result (NULL means no value).
 * On failure, set error using cmdr_error() and return nonzero.
 * user is the opaque pointer supplied when registering this command. */
typedef int (*cmdr_fn)(void *user, size_t argc, const char *const *argv,
                       char **result, char **error);

/* Typed callbacks receive borrowed positional values, including pipeline input
 * in argv[0]. repl is the active registry, including application overrides. */
typedef int (*cmdr_value_fn)(const cmdr *repl, void *user, size_t argc,
                             const cmdr_value *argv, cmdr_value *result,
                             char **error);

typedef struct {
    const char *name;
    const char *usage; /* Command syntax; NULL falls back to name in help. */
    cmdr_fn run;
    void *user;
    cmdr_value_fn run_value;
    int handles_failure; /* Recovery commands can consume CMDR_FAILURE. */
    /* Set exactly one of run, run_value, or pipeline. A pipeline body binds
     * its arguments to $1..$N, requiring exactly N; without references it
     * accepts at most one. The first stage receives $1 as input unless it
     * references $1. The string must outlive the interpreter. */
    const char *pipeline;
    const char *docstring; /* Optional help description, separate from usage. */
    /* Optional: resolve a pipeline body's names in this registry instead of
     * the active one. Must outlive the interpreter. */
    const cmdr *scope;
} cmdr_command;

/* The command table and its strings must outlive the interpreter. */
struct cmdr {
    const cmdr_command *commands;
    size_t count;
    /* Optional fallback registry. Keep the chain acyclic and alive while used. */
    const cmdr *parent;
    /* Internal: composition recursion guard. The interpreter copies this
     * struct and bumps `depth` for nested `.pipeline`/`map`/`try` evaluation.
     * Leave at zero when constructing a registry. */
    unsigned depth;
    /* Internal: `as` bindings visible to nested pipeline strings. Leave NULL. */
    const struct cmdr_frame *frame;
};

/* Search local commands before parent registries. Returns NULL if absent. */
const cmdr_command *cmdr_find(const cmdr *repl, const char *name);
/* Format an error message into *error and return nonzero. *error is set to a
 * fresh malloc-owned string on every failure (never left NULL except when the
 * process is out of memory). Callers initialize *error to NULL and free it. */
int cmdr_error(char **error, const char *format, ...);
/* Call by name without parsing or interpolating arguments. Outputs are reset;
 * caller owns outputs, including on failure. Inputs must not alias outputs. */
int cmdr_call(const cmdr *repl, const char *name, size_t argc,
              const cmdr_value *argv, cmdr_value *result, char **error);
/* input, if non-NULL, is $1 and feeds the first stage as in a pipeline body. */
int cmdr_eval_value(const cmdr *repl, const char *line, const cmdr_value *input,
                    cmdr_value *result, char **error);
/* Bind argv to $1..$N; argv[0] feeds the first stage as in a pipeline body.
 * Requires exactly N arguments, or at most one without references. */
int cmdr_eval_args(const cmdr *repl, const char *line, size_t argc,
                   const cmdr_value *argv, cmdr_value *result, char **error);
/* Check syntax, command names, and arity for argc arguments without running.
 * Pass CMDR_ANY_ARITY to skip the arity check. Caller frees error. */
#define CMDR_ANY_ARITY ((size_t)-1)
int cmdr_check(const cmdr *repl, const char *line, size_t argc, char **error);
/* Check syntax and command names; set *highest to the highest $N referenced
 * (0 if none). Caller frees error. */
int cmdr_arity(const cmdr *repl, const char *line, size_t *highest, char **error);
/* Executes one pipeline. Caller frees result and error, including on failure.
 * Neither pointer may be NULL; both are initialized by this function.
 * Effects from completed stages are not rolled back if a later stage fails. */
int cmdr_eval(cmdr *repl, const char *line, char **result, char **error);

#endif
