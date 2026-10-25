/* Shell-style name patterns, with fnmatch(3). */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <fnmatch.h>
#include <string.h>

/* No flags: * and ? match / and a leading ., and \ escapes the next byte. */
static int match_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                         cmdr_value *result, char **error)
{
    if (argc != 2) return cmdr_error(error, "usage: fnmatch.match NAME PATTERN");
    int status = fnmatch(argv[1].text, argv[0].text, 0);
    if (status == FNM_NOMATCH)
        return cmdr_error(error, "fnmatch.match: %s does not match %s", argv[0].text, argv[1].text), CMDR_MISMATCH;
    if (status) return cmdr_error(error, "fnmatch.match: bad pattern %s", argv[1].text);
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return 0;
}

static const cmdr_command commands[] = {
    {.name = "match", .usage = "match NAME PATTERN",
     .docstring = "Return NAME if it matches the shell-style PATTERN", .run = match_command, .text = 1},
};
const cmdr cmdr_ext_fnmatch = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
