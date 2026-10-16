#define _POSIX_C_SOURCE 200809L
#include "commands.h"
#include "prelude.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runtime definitions. Each def adds a one-command layer above the
 * application commands. Layers are never moved or freed, because cmdr_call
 * may hold a command pointer while its body runs; redefinition shadows. */
typedef struct {
    cmdr layer;
    cmdr_command command;
} definition;

static cmdr application;
static cmdr definitions = {.parent = &application};

/* Return help as text so it composes, e.g. `help | filter list`. */
static int help_command(const cmdr *repl, void *user, size_t argc,
                        const cmdr_value *argv, cmdr_value *result, char **error)
{
    (void)user;
    (void)argv;
    if (argc) return cmdr_error(error, "usage: help");
    size_t width = 0;
    for (const cmdr *group = repl; group; group = group->parent) {
        for (size_t i = 0; i < group->count; ++i) {
            const cmdr_command *command = &group->commands[i];
            if (cmdr_find(repl, command->name) != command) continue;
            const char *usage = command->usage ? command->usage : command->name;
            size_t length = strlen(usage);
            if (length > width) width = length;
        }
    }
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    if (!out) return cmdr_error(error, "out of memory");
    for (const cmdr *group = repl; group; group = group->parent) {
        for (size_t i = 0; i < group->count; ++i) {
            const cmdr_command *command = &group->commands[i];
            if (cmdr_find(repl, command->name) != command) continue;
            const char *usage = command->usage ? command->usage : command->name;
            if (command->docstring && command->docstring[0])
                fprintf(out, "%-*s  %s\n", (int)width, usage, command->docstring);
            else
                fprintf(out, "%s\n", usage);
        }
    }
    fputs("Compose with |; the returned value becomes the next command's first argument.\n"
          "Quote spaces and pipes; $1..$N are arguments; 'as NAME' binds $NAME.\n",
          out);
    int failed = ferror(out);
    if (fclose(out) || failed) {
        free(text);
        return cmdr_error(error, "out of memory");
    }
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = text};
    return 0;
}

static char *duplicate(const char *text)
{
    size_t n = strlen(text) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, text, n);
    return copy;
}

/* A name must be callable as one unquoted word. */
static int valid_name(const char *name)
{
    if (!*name || *name == '$' || *name == '#') return 0;
    for (const char *p = name; *p; ++p)
        if (isspace((unsigned char)*p) || strchr("|'\"\\", *p)) return 0;
    return 1;
}

static int def_command(const cmdr *repl, void *user, size_t argc,
                       const cmdr_value *argv, cmdr_value *result, char **error)
{
    (void)user;
    (void)result;
    if (argc != 2 || argv[0].kind != CMDR_TEXT || argv[1].kind != CMDR_TEXT)
        return cmdr_error(error, "usage: def NAME PIPELINE");
    const char *name = argv[0].text, *body = argv[1].text;
    if (!valid_name(name)) return cmdr_error(error, "def: invalid name '%s'", name);
    if (!strcmp(name, "def") || !strcmp(name, "as"))
        return cmdr_error(error, "def: cannot redefine %s", name);
    char *detail = NULL;
    if (cmdr_check(repl, body, CMDR_ANY_ARITY, &detail)) {
        cmdr_error(error, "def %s: %s", name, detail ? detail : "invalid pipeline");
        free(detail);
        return 1;
    }
    const char *p = body;
    while (isspace((unsigned char)*p)) ++p;
    if (!*p || *p == '#') return cmdr_error(error, "def %s: empty pipeline", name);
    definition *node = calloc(1, sizeof(*node));
    char *copy = duplicate(name), *pipeline = duplicate(body);
    size_t size = strlen(body) + sizeof("def: ");
    char *docstring = malloc(size);
    if (!node || !copy || !pipeline || !docstring) {
        free(node);
        free(copy);
        free(pipeline);
        free(docstring);
        return cmdr_error(error, "out of memory");
    }
    /* One help row per command: collapse whitespace runs in the body. */
    char *d = docstring + snprintf(docstring, size, "def: ");
    for (const char *q = p; *q; ++q)
        if (!isspace((unsigned char)*q)) *d++ = *q;
        else if (!isspace((unsigned char)q[1]) && q[1]) *d++ = ' ';
    *d = '\0';
    /* Lexical scope: the body sees commands as they were at definition, so
     * naming itself calls the command it shadows. */
    node->command = (cmdr_command){.name = copy, .pipeline = pipeline,
                                   .docstring = docstring,
                                   .scope = definitions.parent};
    node->layer = (cmdr){.commands = &node->command, .count = 1,
                         .parent = definitions.parent};
    definitions.parent = &node->layer;
    return 0;
}

void cmdr_commands_init(cmdr *repl)
{
    /* Add application commands here. These take precedence over the prelude.
     * Use .pipeline to compose existing commands, e.g.:
     * {.name = "clean", .usage = "clean [TEXT]", .docstring = "Trim and lowercase text",
     *  .pipeline = "trim | lower"}, */
    static const cmdr_command commands[] = {
        {.name = "help", .usage = "help", .docstring = "Show commands", .run_value = help_command},
        {.name = "def", .usage = "def NAME PIPELINE",
         .docstring = "Define a pipeline command; it may shadow others", .run_value = def_command},
    };
    application = (cmdr){
        .commands = commands,
        .count = sizeof(commands) / sizeof(commands[0]),
        .parent = cmdr_prelude(),
    };
    *repl = (cmdr){.parent = &definitions};
}
