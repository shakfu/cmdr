/* Shell-style path expansion, as Python's glob.glob, with glob(3). */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <glob.h>
#include <string.h>

/* "**" is refused, not read as "*" as glob(3) reads it, so a recursive
 * meaning can come later without changing any result. */
static int glob_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                        cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: glob.glob PATTERN");
    const char *pattern = cmdr_text_of(&argv[0]);
    if (strstr(pattern, "**")) return cmdr_error(error, "glob.glob: ** is reserved for recursive matching");
    glob_t found;
    int status = glob(pattern, 0, NULL, &found);
    if (status == GLOB_NOMATCH) {
        *result = cmdr_list();
        return 0;
    }
    if (status) return cmdr_error(error, "glob.glob %s: %s", pattern, status == GLOB_NOSPACE ? "out of memory" : "read failed");
    *result = cmdr_list();
    for (size_t i = 0; i < found.gl_pathc; ++i)
        cmdr_push(result, cmdr_text(found.gl_pathv[i], strlen(found.gl_pathv[i])));
    globfree(&found);
    return 0;
}

static const cmdr_command commands[] = {
    {.name = "glob", .usage = "glob PATTERN", .docstring = "List the paths that match PATTERN, sorted",
     .run = glob_command, .text = 1},
};
const cmdr cmdr_ext_glob = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
