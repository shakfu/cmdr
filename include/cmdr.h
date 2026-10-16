#ifndef CMDR_H
#define CMDR_H

#include <stddef.h>

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
/* Render lists as newline-delimited text. Use typed values to retain boundaries. */
int cmdr_value_render(const cmdr_value *value, char **text, char **error);

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
    const char *usage;
    cmdr_fn run;
    void *user;
    cmdr_value_fn run_value;
    int handles_failure; /* Recovery commands can consume CMDR_FAILURE. */
    /* Set exactly one of run, run_value, or pipeline. A pipeline body is
     * evaluated with the optional single argument as input (same rules as
     * piping into the first stage). The string must outlive the interpreter. */
    const char *pipeline;
} cmdr_command;

/* The command table and its strings must outlive the interpreter. */
struct cmdr {
    const cmdr_command *commands;
    size_t count;
    /* Optional fallback registry. Keep the chain acyclic and alive while used. */
    const cmdr *parent;
};

/* Search local commands before parent registries. Returns NULL if absent. */
const cmdr_command *cmdr_find(const cmdr *repl, const char *name);
int cmdr_error(char **error, const char *format, ...);
/* Call by name without parsing or interpolating arguments. Outputs are reset;
 * caller owns outputs, including on failure. Inputs must not alias outputs. */
int cmdr_call(const cmdr *repl, const char *name, size_t argc,
              const cmdr_value *argv, cmdr_value *result, char **error);
/* input, if non-NULL, feeds the first stage; normal pipeline rules then apply. */
int cmdr_eval_value(const cmdr *repl, const char *line, const cmdr_value *input,
                    cmdr_value *result, char **error);
/* Executes one pipeline. Caller frees result and error, including on failure.
 * Neither pointer may be NULL; both are initialized by this function.
 * Effects from completed stages are not rolled back if a later stage fails. */
int cmdr_eval(cmdr *repl, const char *line, char **result, char **error);

#endif
