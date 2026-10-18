#include "cmdr.h"

static int noop(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                cmdr_value *result, char **error)
{
    return 0;
}

/* A primitive, a built-in, a prelude definition, a duplicate, and one new name. */
static const cmdr_command commands[] = {
    {.name = "split", .run = noop},
    {.name = "help", .run = noop},
    {.name = "last", .run = noop},
    {.name = "fresh", .run = noop},
    {.name = "fresh", .run = noop},
};
const cmdr cmdr_ext_clash = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
