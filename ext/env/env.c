#include "cmdr.h"

#include <stdlib.h>
#include <string.h>

static int env_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                       cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: env.get NAME");
    const char *value = getenv(argv[0].text);
    if (!value) return cmdr_error(error, "env.get: %s is not set", argv[0].text), CMDR_MISMATCH;
    *result = cmdr_text(value, strlen(value));
    return 0;
}

static const cmdr_command commands[] = {
    {.name = "get", .usage = "get NAME", .docstring = "Return an environment variable",
     .run = env_command, .text = 1},
};
const cmdr cmdr_ext_env = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
