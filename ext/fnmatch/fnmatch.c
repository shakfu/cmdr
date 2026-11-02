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
    const char *name = cmdr_text_of(&argv[0]), *pattern = cmdr_text_of(&argv[1]);
    int status = fnmatch(pattern, name, 0);
    if (status == FNM_NOMATCH)
        return cmdr_error(error, "fnmatch.match: %s does not match %s", name, pattern), CMDR_MISMATCH;
    if (status) return cmdr_error(error, "fnmatch.match: bad pattern %s", pattern);
    *result = cmdr_text(name, strlen(name));
    return 0;
}

static const cmdr_command commands[] = {
    {.name = "match", .usage = "match NAME PATTERN",
     .docstring = "Return NAME if it matches the shell-style PATTERN", .run = match_command, .text = 1},
};
const cmdr cmdr_ext_fnmatch = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
