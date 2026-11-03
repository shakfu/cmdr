/* Standard input and output while a line runs. A line's result prints only
 * when the line ends; these commands read and write before then. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* Write VALUE to out as a line's result prints; keep a copy in *result if keep. */
static int emit(const char *name, FILE *out, int keep, size_t argc, const cmdr_value *argv,
                cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: io.%s VALUE", name);
    char *detail = NULL;
    if (cmdr_renderable(&argv[0], &detail)) {
        cmdr_error(error, "io.%s: %s", name, detail);
        free(detail);
        return 1;
    }
    if (cmdr_print(&argv[0], out)) return cmdr_error(error, "io.%s: output failed", name);
    if (keep) return cmdr_clone(&argv[0], result, error);
    *result = (cmdr_value){0};
    return 0;
}

COMMAND(print_command) { return emit("print", stdout, 0, argc, argv, result, error); }
COMMAND(tee_command) { return emit("tee", stdout, 1, argc, argv, result, error); }
COMMAND(eprint_command) { return emit("eprint", stderr, 0, argc, argv, result, error); }
COMMAND(etee_command) { return emit("etee", stderr, 1, argc, argv, result, error); }

/* One line of stdin without its \n, as fs.each-line splits; end of input
 * mismatches. The prompt goes to stderr, so it stays out of piped output. */
COMMAND(input_command)
{
    if (argc > 1) return cmdr_error(error, "usage: io.input [PROMPT]");
    if (argc) {
        fputs(cmdr_text_of(&argv[0]), stderr);
        fflush(stderr);
    }
    char *line = NULL;
    size_t size = 0;
    /* SIGINT ends a blocking read with EINTR, since cmdr's handler lacks SA_RESTART.
     * One that came before the read sets only the flag, so check it first and last. */
    ssize_t length = cmdr_interrupted() ? -1 : getline(&line, &size, stdin);
    int status = 0;
    if (length < 0) {
        if (cmdr_interrupted()) status = cmdr_error(error, "io.input: interrupted");
        else if (!ferror(stdin)) status = (cmdr_error(error, "io.input: end of input"), CMDR_MISMATCH);
        else status = cmdr_error(error, "io.input: read failed");
        clearerr(stdin); /* A REPL reads on after end of input. */
    } else if (memchr(line, '\0', (size_t)length)) {
        status = cmdr_error(error, "io.input: binary data is not supported");
    } else {
        if (length && line[length - 1] == '\n') line[--length] = '\0';
        *result = cmdr_text(line, (size_t)length);
    }
    free(line);
    return status;
}

static const cmdr_command commands[] = {
    {.name = "input", .usage = "input [PROMPT]", .docstring = "Read a line from stdin, after writing PROMPT to stderr",
     .run = input_command, .text = 1},
    {.name = "print", .usage = "print VALUE", .docstring = "Write VALUE to stdout now; return nothing",
     .run = print_command},
    {.name = "tee", .usage = "tee VALUE", .docstring = "Write VALUE to stdout now; return VALUE",
     .run = tee_command},
    {.name = "eprint", .usage = "eprint VALUE", .docstring = "Write VALUE to stderr now; return nothing",
     .run = eprint_command},
    {.name = "etee", .usage = "etee VALUE", .docstring = "Write VALUE to stderr now; return VALUE",
     .run = etee_command},
};
const cmdr cmdr_ext_io = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
