#ifndef CMDR_H
#define CMDR_H

#include <stddef.h>
#include <stdio.h>

/* Status for a predicate that does not hold, as opposed to an error (1).
 * keep, not, and try treat only this status as false. */
#define CMDR_MISMATCH 2

typedef enum { CMDR_NONE, CMDR_TEXT, CMDR_LIST, CMDR_BLOCK } cmdr_kind;

typedef struct cmdr_code cmdr_code;

/* An owned value: text without NUL bytes, a list of values, or a block. A
 * block's text is its source, "(a b) pipeline" or "pipeline"; its items are
 * the values it captured; code is its compiled form, shared by copies. {0} is
 * none. */
typedef struct cmdr_value {
    cmdr_kind kind;
    char *text;
    struct cmdr_value *items;
    size_t count;
    cmdr_code *code;
} cmdr_value;

typedef struct cmdr cmdr;

/* argv is borrowed, including blocks' captured values; clone what you keep. A
 * piped value comes first. Set *result and return 0, or return nonzero
 * through cmdr_error. repl resolves names the command runs. */
typedef int (*cmdr_fn)(const cmdr *repl, size_t argc, const cmdr_value *argv,
                       cmdr_value *result, char **error);

typedef struct {
    const char *name;
    const char *usage;     /* Syntax shown by help; NULL shows the name. */
    const char *docstring; /* Description shown by help. */
    cmdr_fn run;           /* Set run, pipeline, or block. */
    int text;              /* run accepts only text arguments. */
    const char *pipeline;  /* Block source, as written inside { }; compiled per call. */
    const cmdr_value *block; /* A compiled block, as def makes; set instead of pipeline. */
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
/* Evaluate a line, which has no input. */
int cmdr_eval(const cmdr *repl, const char *line, cmdr_value *result, char **error);
/* Call a block with argv bound to its parameters. */
int cmdr_run(const cmdr *repl, const cmdr_value *block, size_t argc,
             const cmdr_value *argv, cmdr_value *result, char **error);
/* Fail unless the block takes exactly argc arguments. */
int cmdr_block_arity(const cmdr_value *block, size_t argc, char **error);

#endif
