#include "commands.h"
#include "prelude.h"

#include <stdio.h>
#include <string.h>

static int help_command(void *user, size_t argc, const char *const *argv,
                        char **result, char **error)
{
    (void)user;
    (void)argv;
    (void)result;
    if (argc) return cmdr_error(error, "usage: help");
    cmdr repl;
    cmdr_commands_init(&repl);
    size_t width = 0;
    for (const cmdr *group = &repl; group; group = group->parent) {
        for (size_t i = 0; i < group->count; ++i) {
            const cmdr_command *command = &group->commands[i];
            if (cmdr_find(&repl, command->name) != command) continue;
            const char *usage = command->usage ? command->usage : command->name;
            size_t length = strlen(usage);
            if (length > width) width = length;
        }
    }
    for (const cmdr *group = &repl; group; group = group->parent) {
        for (size_t i = 0; i < group->count; ++i) {
            const cmdr_command *command = &group->commands[i];
            if (cmdr_find(&repl, command->name) != command) continue;
            const char *usage = command->usage ? command->usage : command->name;
            fputs(usage, stdout);
            if (command->docstring && command->docstring[0]) {
                for (size_t padding = width - strlen(usage); padding > 0; --padding)
                    putchar(' ');
                printf("  %s", command->docstring);
            }
            putchar('\n');
        }
    }
    puts("Compose with |; the returned value becomes the next command's first argument.");
    puts("Use single/double quotes or backslash escapes for spaces and literal pipes.");
    return ferror(stdout) ? cmdr_error(error, "output failed") : 0;
}

void cmdr_commands_init(cmdr *repl)
{
    /* Add application commands here. These take precedence over the prelude.
     * Use .pipeline to compose existing commands, e.g.:
     * {.name = "clean", .usage = "clean [TEXT]", .docstring = "Trim and lowercase text",
     *  .pipeline = "trim | lower"}, */
    static const cmdr_command commands[] = {
        {.name = "help", .usage = "help", .docstring = "Show commands", .run = help_command},
    };
    *repl = (cmdr){
        .commands = commands,
        .count = sizeof(commands) / sizeof(commands[0]),
        .parent = cmdr_prelude(),
    };
}
