#ifndef CMDR_H
#define CMDR_H

#include <stddef.h>
#include <stdio.h>

/* Status for a predicate that does not hold, as opposed to an error (1).
 * keep, not, and try treat only this status as false. */
#define CMDR_MISMATCH 2

typedef enum { CMDR_NONE, CMDR_TEXT, CMDR_LIST } cmdr_kind;

/* An owned value: text without NUL bytes, or a list of values. {0} is none. */
typedef struct cmdr_value {
    cmdr_kind kind;
    char *text;
    struct cmdr_value *items;
    size_t count;
} cmdr_value;

typedef struct cmdr cmdr;

/* argv is borrowed; a piped value comes first. Set *result and return 0, or
 * return nonzero through cmdr_error. repl resolves names the command runs. */
typedef int (*cmdr_fn)(const cmdr *repl, size_t argc, const cmdr_value *argv,
                       cmdr_value *result, char **error);

typedef struct {
    const char *name;
    const char *usage;     /* Syntax shown by help; NULL shows the name. */
    const char *docstring; /* Description shown by help. */
    cmdr_fn run;           /* Set run or pipeline. */
    int text;              /* run accepts only text arguments. */
    const char *pipeline;  /* Body taking $1..$N. */
    const cmdr *scope;     /* Resolve the body here; NULL uses the caller's. */
} cmdr_command;

/* A command table searched before its parent. */
struct cmdr {
    const cmdr_command *commands;
    size_t count;
    const cmdr *parent;
};

extern const cmdr cmdr_prelude;

/* Allocation failure exits the process. */
void *cmdr_realloc(void *p, size_t count, size_t size);
char *cmdr_strndup(const char *text, size_t n);

cmdr_value cmdr_text(const char *text, size_t n);
/* Move item into list. */
void cmdr_push(cmdr_value *list, cmdr_value item);
void cmdr_free(cmdr_value *value);
/* Fails on values nested too deeply, which bounds recursion elsewhere. */
int cmdr_clone(const cmdr_value *value, cmdr_value *out, char **error);
/* Write text as is and list items each followed by a newline. */
void cmdr_render(const cmdr_value *value, FILE *out);

/* Set *error to a formatted, malloc-owned message and return 1. */
int cmdr_error(char **error, const char *format, ...);

const cmdr_command *cmdr_find(const cmdr *repl, const char *name);
int cmdr_call(const cmdr *repl, const char *name, size_t argc,
              const cmdr_value *argv, cmdr_value *result, char **error);
/* Evaluate line with argv bound to $1..$N; argv[0] feeds the first stage.
 * Requires exactly N arguments, or at most one without references. */
int cmdr_eval(const cmdr *repl, const char *line, size_t argc,
              const cmdr_value *argv, cmdr_value *result, char **error);
/* Check syntax, command names, and arity without running. */
#define CMDR_ANY_ARITY ((size_t)-1)
int cmdr_check(const cmdr *repl, const char *line, size_t argc, char **error);

#endif
