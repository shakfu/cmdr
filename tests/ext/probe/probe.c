#include "cmdr.h"

static int probe(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                 cmdr_value *result, char **error)
{
    if (argc) return cmdr_error(error, "usage: probe");
    *result = cmdr_text("probed", 6);
    return 0;
}

static const cmdr_command commands[] = {
    {.name = "probe", .usage = "probe", .docstring = "Return probed", .run = probe},
};
const cmdr cmdr_ext_probe = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
