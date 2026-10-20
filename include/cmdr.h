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
 * none. capacity is the slots allocated in items, kept by cmdr_push; 0 means
 * exactly count, as for a list allocated whole. */
typedef struct cmdr_value {
    cmdr_kind kind;
    char *text;
    struct cmdr_value *items;
    size_t count;
    cmdr_code *code;
    size_t capacity;
} cmdr_value;

typedef struct cmdr cmdr;

/* argv is borrowed, including blocks' captured values; clone what you keep. A
 * piped value comes first. Set *result and return 0, or return nonzero
 * through cmdr_error. repl resolves names the command runs; user is the
 * command's user field. */
typedef int (*cmdr_fn)(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                       cmdr_value *result, char **error);

typedef struct {
    const char *name;
    const char *usage;     /* Syntax shown by help; NULL shows the name. */
    const char *docstring; /* Description shown by help. */
    cmdr_fn run;           /* Set run, block, or value. */
    void *user;            /* Passed to run. */
    int text;              /* run accepts only text arguments. */
    const cmdr_value *block; /* A compiled block, as def makes. */
    /* A constant, as as makes: calling the command returns a copy, and $NAME
     * refers to it. It must outlive every line compiled against it. */
    const cmdr_value *value;
} cmdr_command;

/* A command table searched before its parent. */
struct cmdr {
    const cmdr_command *commands;
    size_t count;
    const cmdr *parent;
};

/* Commands written in C; composed ones are defs in prelude.cmdr. cmdr_pure
 * omits read, write, append, files, run, and run-in. */
extern const cmdr cmdr_primitives, cmdr_pure;

/* Allocation failure calls the out-of-memory handler, then exits the process
 * with status 1. */
void *cmdr_realloc(void *p, size_t count, size_t size);
/* Set the handler, with the bytes requested, or SIZE_MAX on overflow. It may
 * log, save host state, or end the process itself. It must not longjmp: that
 * leaks the failing call's values and leaves its call depth counted. Set it
 * before any thread evaluates; NULL restores the default. */
void cmdr_set_out_of_memory(void (*handler)(void *data, size_t size), void *data);
char *cmdr_strndup(const char *text, size_t n);

/* Set or clear the interrupt flag. While it is set, cmdr_call fails with
 * "interrupted", try does not catch it, and run stops its program with
 * SIGTERM, then SIGKILL a second later. It is process-wide, and safe to call
 * from a signal handler. The host clears it. */
void cmdr_interrupt(int on);
int cmdr_interrupted(void);

cmdr_value cmdr_text(const char *text, size_t n);
/* Move item into list, doubling its capacity when full. */
void cmdr_push(cmdr_value *list, cmdr_value item);
void cmdr_free(cmdr_value *value);
/* Fails on values nested too deeply, which bounds recursion elsewhere. */
int cmdr_clone(const cmdr_value *value, cmdr_value *out, char **error);
/* Write text as is and list items each followed by a newline. */
void cmdr_render(const cmdr_value *value, FILE *out);

/* Parse a finite decimal number, or fail with "invalid finite number: TEXT".
 * Numbers use '.' only while LC_NUMERIC is "C", so hosts must not change it. */
int cmdr_number(const char *text, double *value, char **error);
/* A number as arithmetic results print: the fewest digits, up to 17, that read
 * back as the same double. */
cmdr_value cmdr_number_value(double value);
/* Parse nonnegative decimal integer text, or fail. */
int cmdr_natural(const cmdr_value *value, size_t *n, char **error);

/* A short, one-line, malloc-owned description of value for a message. */
char *cmdr_describe(const cmdr_value *value);
/* Set *error to a formatted, malloc-owned message and return 1. */
int cmdr_error(char **error, const char *format, ...);

const cmdr_command *cmdr_find(const cmdr *repl, const char *name);
int cmdr_call(const cmdr *repl, const char *name, size_t argc,
              const cmdr_value *argv, cmdr_value *result, char **error);
/* Evaluate a line, which has no input. */
int cmdr_eval(const cmdr *repl, const char *line, cmdr_value *result, char **error);
/* Call a block with argv bound to its parameters; omitted trailing
 * parameters take their defaults. Its commands resolve where it was written,
 * not in the caller. */
int cmdr_run(const cmdr_value *block, size_t argc, const cmdr_value *argv,
             cmdr_value *result, char **error);
/* Fail unless the block accepts argc arguments: at least its parameters
 * without defaults, at most all of them. */
int cmdr_block_arity(const cmdr_value *block, size_t argc, char **error);

/* Write value as cmdr_render does, ending with a newline, and flush. Nothing
 * is written for none. Returns nonzero if writing failed. */
int cmdr_print(const cmdr_value *value, FILE *out);
/* Append line, n bytes, to the pipeline being read. Return 1 if the pipeline
 * continues: its last nonblank character is an unescaped '|', or the line is
 * blank or a comment inside a continuation and was skipped. */
int cmdr_join(char **pipeline, size_t *used, const char *line, size_t n);

/* help, def, and load in front of a host's command table. A session is used
 * by one thread at a time; sessions on different threads are independent. */
typedef struct cmdr_session cmdr_session;

/* commands, usually ending in &cmdr_primitives, must outlive the session.
 * effects adds load, import, and the prelude's file and program commands; without it,
 * end commands in &cmdr_pure for a session with no file or process access.
 * Scripts that load runs print their values to out; NULL discards them. */
cmdr_session *cmdr_session_open(const cmdr *commands, int effects, FILE *out);
/* Free the session's definitions. Blocks it made must not run afterwards. */
void cmdr_session_close(cmdr_session *session);
/* All commands, including later definitions; for lookup and completion. */
const cmdr *cmdr_session_commands(const cmdr_session *session);
/* Evaluate a line against the commands defined so far, so a later def does
 * not change what its blocks call. */
int cmdr_session_eval(cmdr_session *session, const char *line, cmdr_value *result,
                      char **error);
/* Evaluate a script's pipelines, printing values to the session's out. Stops
 * at the first failure, with *error starting "PATH:LINE: ". If PATH exists,
 * script-file and script-dir return it and its directory in code the
 * script compiles, and script-args returns an empty list. */
int cmdr_session_load(cmdr_session *session, FILE *in, const char *path, char **error);
/* cmdr_session_load, with argv as the script's script-args. */
int cmdr_session_load_args(cmdr_session *session, FILE *in, const char *path, size_t argc,
                           const char *const *argv, char **error);
/* Evaluate a script held in memory, as cmdr_session_load does; name is its
 * path in messages. */
int cmdr_session_source(cmdr_session *session, const char *source, const char *name,
                        char **error);
/* Load the standard prelude, embedded by the build: src/prelude.cmdr, then
 * src/prelude-effects.cmdr in a session with effects. */
int cmdr_session_prelude(cmdr_session *session, char **error);

#endif
