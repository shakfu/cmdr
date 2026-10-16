#include "commands.h"
#include "prelude.h"

#include <stdio.h>

static int help_command(void *user, size_t argc, const char *const *argv,
                        char **result, char **error)
{
    (void)user;
    (void)argv;
    (void)result;
    if (argc) return cmdr_error(error, "usage: help");
    cmdr repl;
    cmdr_commands_init(&repl);
    for (const cmdr *group = &repl; group; group = group->parent) {
        for (size_t i = 0; i < group->count; ++i) {
            const cmdr_command *command = &group->commands[i];
            if (cmdr_find(&repl, command->name) == command) puts(command->usage);
        }
    }
    puts("Compose with |; the returned value becomes the next command's first argument.");
    puts("Use single/double quotes or backslash escapes for spaces and literal pipes.");
    return ferror(stdout) ? cmdr_error(error, "output failed") : 0;
}

void cmdr_commands_init(cmdr *repl)
{
    /* Add application commands here. These take precedence over the prelude. */
    static const cmdr_command commands[] = {
        {"help", "help                      Show commands", help_command, NULL, NULL, 0},
    };
    *repl = (cmdr){
        .commands = commands,
        .count = sizeof(commands) / sizeof(commands[0]),
        .parent = cmdr_prelude(),
    };
}
