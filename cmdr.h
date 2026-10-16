#ifndef CMDR_H
#define CMDR_H

#include <stddef.h>

typedef struct cmdr cmdr;

/* Arguments are borrowed for the duration of the call. On success, return a
 * malloc-owned, NUL-terminated string through result (NULL means no value).
 * On failure, set error using cmdr_error() and return nonzero.
 * user is the opaque pointer supplied when registering this command. */
typedef int (*cmdr_fn)(void *user, size_t argc, const char *const *argv,
                       char **result, char **error);

typedef struct {
    const char *name;
    const char *usage;
    cmdr_fn run;
    void *user;
} cmdr_command;

/* The command table and its strings must outlive the interpreter. */
struct cmdr {
    const cmdr_command *commands;
    size_t count;
};

int cmdr_error(char **error, const char *format, ...);
/* Executes one pipeline. Caller frees result and error, including on failure.
 * Neither pointer may be NULL; both are initialized by this function.
 * Effects from completed stages are not rolled back if a later stage fails. */
int cmdr_eval(cmdr *repl, const char *line, char **result, char **error);

#endif
