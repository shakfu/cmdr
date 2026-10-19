#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* src/prelude.cmdr and src/prelude-effects.cmdr, embedded by the build. */
extern const char cmdr_prelude_source[], cmdr_prelude_effects_source[];

/* Names resolve through head.parent: each def pushes a one-command layer in
 * front of base, which holds the built-ins and then the host's commands.
 * Layers live until close, since a running body may still use an earlier one. */
struct cmdr_session {
    cmdr head;
    cmdr base;
    cmdr_command builtins[6];
    int effects;
    FILE *out;
};

/* A def's layer comes first, so a layer pointer is its node. A def uses
 * commands[0]. A script pushes a layer of the script commands, whose user is
 * the node holding its path and arguments; after it ends, a layer whose user
 * is the outer script's node, or NULL. */
typedef struct {
    cmdr layer;
    cmdr_command commands[3];
    cmdr_value block;
    char *path;
    cmdr_value args;
} definition;

/* The node of the script whose layer user is, or an error naming command. */
static const definition *script_of(void *user, const char *command, size_t argc, char **error)
{
    const definition *node = user;
    if (argc) cmdr_error(error, "usage: %s", command);
    else if (!node || !node->path) cmdr_error(error, "%s: not in a script file", command);
    else return node;
    return NULL;
}

static int script_file_command(const cmdr *repl, void *user, size_t argc,
                               const cmdr_value *argv, cmdr_value *result, char **error)
{
    const definition *node = script_of(user, "script-file", argc, error);
    if (!node) return 1;
    *result = cmdr_text(node->path, strlen(node->path));
    return 0;
}

static int script_dir_command(const cmdr *repl, void *user, size_t argc,
                              const cmdr_value *argv, cmdr_value *result, char **error)
{
    const definition *node = script_of(user, "script-dir", argc, error);
    if (!node) return 1;
    char *copy = cmdr_strndup(node->path, strlen(node->path)), *dir = dirname(copy);
    *result = cmdr_text(dir, strlen(dir));
    free(copy);
    return 0;
}

static int script_args_command(const cmdr *repl, void *user, size_t argc,
                               const cmdr_value *argv, cmdr_value *result, char **error)
{
    const definition *node = script_of(user, "script-args", argc, error);
    return !node || cmdr_clone(&node->args, result, error);
}

static int script_command(const cmdr_command *command)
{
    return command->run == script_file_command || command->run == script_dir_command
        || command->run == script_args_command;
}

/* Return help as text so it composes, e.g. `help | filter list`. */
static int help_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                        cmdr_value *result, char **error)
{
    if (argc) return cmdr_error(error, "usage: help");
    /* Tables in lookup order, then definitions oldest first, which reverses
     * their lookup order. Each def is a layer of one command with a block. */
    const cmdr **groups = NULL;
    size_t ngroups = 0;
    for (const cmdr *group = repl; group; group = group->parent) {
        groups = cmdr_realloc(groups, ngroups + 1, sizeof(*groups));
        groups[ngroups++] = group;
    }
    const cmdr_command **active = NULL;
    size_t count = 0;
    int width = 0;
    for (size_t pass = 0; pass < 2; ++pass)
        for (size_t k = 0; k < ngroups; ++k) {
            const cmdr *group = groups[pass ? ngroups - 1 - k : k];
            int def = group->count && group->commands[0].block;
            if (def != (int)pass) continue;
            /* A script's layers shadow the built-ins, which list in their place. */
            if (group->count && script_command(&group->commands[0])) continue;
            for (size_t i = 0; i < group->count; ++i) {
                const cmdr_command *command = &group->commands[i];
                const cmdr_command *found = cmdr_find(repl, command->name);
                if (found != command && !(found && script_command(command)
                                          && found->run == command->run))
                    continue; /* Shadowed. */
                active = cmdr_realloc(active, count + 1, sizeof(*active));
                active[count++] = command;
                const char *usage = command->usage ? command->usage : command->name;
                if ((int)strlen(usage) > width) width = (int)strlen(usage);
            }
        }
    free(groups);
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    if (!out) { free(active); return cmdr_error(error, "help: %s", strerror(errno)); }
    for (size_t i = 0; i < count; ++i)
        fprintf(out, "%-*s  %s\n", width,
                active[i]->usage ? active[i]->usage : active[i]->name,
                active[i]->docstring ? active[i]->docstring : "");
    fputs("Compose with |; the returned value becomes the next command's first argument.\n"
          "Quote spaces and pipes; { (a b) ... } is a block taking $a and $b.\n", out);
    fclose(out);
    free(active);
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = text};
    return 0;
}

/* A name must be callable as one unquoted word. */
static int valid_name(const char *name)
{
    if (!*name || *name == '$' || *name == '#') return 0;
    for (const char *p = name; *p; ++p)
        if (isspace((unsigned char)*p) || strchr("|'\"\\", *p)) return 0;
    return 1;
}

/* Help syntax from the block: "NAME A B" for { (a b) ... }, "NAME VALUE" for
 * { ... }, which takes one piped input. */
static char *usage_of(const char *name, const char *block)
{
    const char *p = block;
    while (isspace((unsigned char)*p)) ++p;
    size_t n = strlen(name);
    char *usage = cmdr_realloc(NULL, n + strlen(p) + sizeof(" VALUE"), 1), *u = usage + n;
    memcpy(usage, name, n);
    if (*p != '(') u += sprintf(u, " VALUE");
    else
        for (++p; *p && *p != ')'; ++p)
            if (!isspace((unsigned char)*p)) {
                if (p[-1] == '(' || isspace((unsigned char)p[-1])) *u++ = ' ';
                *u++ = (char)toupper((unsigned char)*p);
            }
    *u = '\0';
    return usage;
}

static int def_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                       cmdr_value *result, char **error)
{
    cmdr_session *session = user;
    if (argc < 2 || argc > 3 || argv[0].kind != CMDR_TEXT || argv[1].kind != CMDR_BLOCK
        || (argc == 3 && argv[2].kind != CMDR_TEXT))
        return cmdr_error(error, "usage: def NAME BLOCK [DOC]");
    const char *name = argv[0].text, *body = argv[1].text;
    if (!valid_name(name)) return cmdr_error(error, "def: invalid name '%s'", name);
    if (!strcmp(name, "def")) return cmdr_error(error, "def: cannot redefine def");
    /* Its line checked the block; a block inside another captures names. */
    if (argv[1].count) return cmdr_error(error, "def %s: block uses enclosing parameters", name);
    const char *p = body;
    while (isspace((unsigned char)*p)) ++p;
    if (!*p) return cmdr_error(error, "def %s: empty block", name);
    /* One help row per command: DOC, or the body with whitespace runs collapsed. */
    char *docstring = cmdr_realloc(NULL, strlen(p) + sizeof("def: "), 1), *d = docstring;
    d += sprintf(d, "def: ");
    for (; *p; ++p)
        if (!isspace((unsigned char)*p)) *d++ = *p;
        else if (p[1] && !isspace((unsigned char)p[1])) *d++ = ' ';
    *d = '\0';
    if (argc == 3) {
        free(docstring);
        docstring = cmdr_strndup(argv[2].text, strlen(argv[2].text));
    }
    definition *node = cmdr_realloc(NULL, 1, sizeof(*node));
    *node = (definition){0};
    if (cmdr_clone(&argv[1], &node->block, error)) { free(node); free(docstring); return 1; }
    /* The block resolves names where its line compiled, before this layer,
     * so naming itself calls the command it shadows. */
    node->commands[0] = (cmdr_command){.name = cmdr_strndup(name, strlen(name)),
                                       .usage = usage_of(name, body),
                                       .docstring = docstring,
                                       .block = &node->block};
    node->layer = (cmdr){node->commands, 1, session->head.parent};
    session->head.parent = &node->layer;
    return 0;
}

/* Run a script's lines in this session, so its definitions remain. */
static int load_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                        cmdr_value *result, char **error)
{
    if (!argc) return cmdr_error(error, "usage: load PATH [ARG ...]");
    FILE *file = fopen(argv[0].text, "r");
    if (!file) return cmdr_error(error, "load %s: %s", argv[0].text, strerror(errno));
    const char **args = cmdr_realloc(NULL, argc, sizeof(*args));
    for (size_t i = 1; i < argc; ++i) args[i - 1] = argv[i].text;
    char *inner;
    int status = cmdr_session_load_args(user, file, argv[0].text, argc - 1, args, &inner);
    free(args);
    fclose(file);
    if (status) cmdr_error(error, "load %s failed: %s", argv[0].text, inner);
    free(inner);
    return status ? 1 : 0;
}

cmdr_session *cmdr_session_open(const cmdr *commands, int effects, FILE *out)
{
    cmdr_session *s = cmdr_realloc(NULL, 1, sizeof(*s));
    *s = (cmdr_session){
        .builtins = {
            {.name = "help", .usage = "help", .docstring = "Show commands", .run = help_command},
            {.name = "def", .usage = "def NAME BLOCK [DOC]",
             .docstring = "Define a command; it may shadow others", .run = def_command, .user = s},
            {.name = "script-file", .usage = "script-file",
             .docstring = "Return the path of the script file this code is written in",
             .run = script_file_command},
            {.name = "script-dir", .usage = "script-dir",
             .docstring = "Return the directory of the script file this code is written in",
             .run = script_dir_command},
            {.name = "script-args", .usage = "script-args",
             .docstring = "Return the arguments of the script file this code is written in",
             .run = script_args_command},
            {.name = "load", .usage = "load PATH [ARG ...]",
             .docstring = "Run a script in this session, with arguments",
             .run = load_command, .user = s, .text = 1},
        },
        .effects = effects,
        .out = out,
    };
    /* load is last, so a session without effects omits it. */
    s->base = (cmdr){s->builtins, effects ? 6 : 5, commands};
    s->head.parent = &s->base;
    return s;
}

void cmdr_session_close(cmdr_session *session)
{
    if (!session) return;
    for (const cmdr *layer = session->head.parent; layer != &session->base;) {
        definition *node = (definition *)layer;
        layer = layer->parent;
        if (node->commands[0].block) { /* Script layers share the built-ins' strings. */
            free((char *)node->commands[0].name);
            free((char *)node->commands[0].usage);
            free((char *)node->commands[0].docstring);
        }
        free(node->path);
        cmdr_free(&node->args);
        cmdr_free(&node->block);
        free(node);
    }
    free(session);
}

const cmdr *cmdr_session_commands(const cmdr_session *session)
{
    return &session->head;
}

int cmdr_session_eval(cmdr_session *session, const char *line, cmdr_value *result,
                      char **error)
{
    return cmdr_eval(session->head.parent, line, result, error);
}

int cmdr_print(const cmdr_value *value, FILE *out)
{
    if (value->kind == CMDR_NONE) return 0;
    cmdr_render(value, out);
    size_t n = value->text ? strlen(value->text) : 0;
    if (value->kind == CMDR_LIST ? !value->count : (!n || value->text[n - 1] != '\n'))
        fputc('\n', out);
    return fflush(out) != 0;
}

static int blank_or_comment(const char *line)
{
    while (isspace((unsigned char)*line)) ++line;
    return !*line || *line == '#';
}

int cmdr_join(char **pipeline, size_t *used, const char *line, size_t n)
{
    if (*used && blank_or_comment(line)) return 1;
    *pipeline = cmdr_realloc(*pipeline, *used + n + 1, 1);
    memcpy(*pipeline + *used, line, n);
    (*pipeline)[*used += n] = '\0';
    size_t end = *used;
    while (end && isspace((unsigned char)(*pipeline)[end - 1])) --end;
    return end && (*pipeline)[end - 1] == '|' && (end < 2 || (*pipeline)[end - 2] != '\\')
        && !blank_or_comment(*pipeline);
}

/* Push a layer of the script commands, reading user's path and arguments. */
static definition *push_script(cmdr_session *session, const definition *user)
{
    definition *node = cmdr_realloc(NULL, 1, sizeof(*node));
    *node = (definition){0};
    for (size_t i = 0; i < 3; ++i) {
        node->commands[i] = session->builtins[2 + i];
        node->commands[i].user = (void *)user;
    }
    node->layer = (cmdr){node->commands, 3, session->head.parent};
    session->head.parent = &node->layer;
    return node;
}

static int run_script(cmdr_session *session, FILE *in, const char *path, char **error);

int cmdr_session_load(cmdr_session *session, FILE *in, const char *path, char **error)
{
    return cmdr_session_load_args(session, in, path, 0, NULL, error);
}

int cmdr_session_load_args(cmdr_session *session, FILE *in, const char *path, size_t argc,
                           const char *const *argv, char **error)
{
    /* Lines compiled while the script runs see its path and arguments. */
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) return run_script(session, in, path, error);
    /* The outer node lives until the session closes. */
    const definition *outer = cmdr_find(session->head.parent, "script-file")->user;
    definition *node = push_script(session, NULL);
    node->path = cmdr_strndup(resolved, strlen(resolved));
    node->args = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < argc; ++i) cmdr_push(&node->args, cmdr_text(argv[i], strlen(argv[i])));
    for (size_t i = 0; i < 3; ++i) node->commands[i].user = node;
    int status = run_script(session, in, path, error);
    push_script(session, outer);
    return status;
}

static int run_script(cmdr_session *session, FILE *in, const char *path, char **error)
{
    *error = NULL;
    char *line = NULL, *pipeline = NULL;
    size_t capacity = 0, used = 0;
    unsigned long number = 0, first = 0;
    int status = 0;
    for (;;) {
        ssize_t n = getline(&line, &capacity, in);
        if (n >= 0) {
            ++number;
            if (!used) first = number;
            if (cmdr_join(&pipeline, &used, line, (size_t)n)) continue;
        }
        if (!used) break;
        used = 0;
        cmdr_value value;
        char *inner;
        status = cmdr_session_eval(session, pipeline, &value, &inner);
        if (!status && session->out && cmdr_print(&value, session->out))
            status = cmdr_error(&inner, "output failed");
        cmdr_free(&value);
        if (status) cmdr_error(error, "%s:%lu: %s", path, first, inner ? inner : "command failed");
        free(inner);
        if (status || n < 0) break;
    }
    if (!status && ferror(in)) status = cmdr_error(error, "%s: %s", path, strerror(errno));
    free(line);
    free(pipeline);
    return status;
}

int cmdr_session_source(cmdr_session *session, const char *source, const char *name,
                        char **error)
{
    *error = NULL;
    size_t n = strlen(source);
    if (!n) return 0; /* fmemopen may reject an empty buffer. */
    FILE *in = fmemopen((void *)source, n, "r");
    if (!in) return cmdr_error(error, "%s: %s", name, strerror(errno));
    int status = run_script(session, in, name, error);
    fclose(in);
    return status;
}

int cmdr_session_prelude(cmdr_session *session, char **error)
{
    *error = NULL;
    int status = cmdr_session_source(session, cmdr_prelude_source, "prelude.cmdr", error);
    if (!status && session->effects)
        status = cmdr_session_source(session, cmdr_prelude_effects_source, "prelude-effects.cmdr", error);
    return status;
}
