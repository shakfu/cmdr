#define _XOPEN_SOURCE 700 /* POSIX-2008 with XSI, for realpath. */
#include "cmdr.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* src/prelude.cmdr and src/prelude-effects.cmdr, embedded by the build. */
extern const char cmdr_prelude_source[], cmdr_prelude_effects_source[];

/* A def's layer comes first, so a layer pointer is its node. A def uses
 * commands[0] and owns block; an as binding uses commands[0] and owns value.
 * An import's alias also uses commands[0], whose block, or binding node as
 * user, is the module's. A script pushes a layer of the script commands,
 * whose user is the node holding its path and arguments; after it ends, a
 * layer whose user is the outer script's node, or NULL. */
typedef struct definition {
    cmdr layer;
    cmdr_command commands[3];
    cmdr_value block;
    cmdr_value value;
    char *path;
    cmdr_value args;
    struct definition *next; /* The session's previous node. */
} definition;

/* A module imported, or being imported, by its resolved path. */
typedef struct {
    char *path;
    int loading;
    const cmdr_command **exports;
    size_t count;
} module;

/* Names resolve through head.parent: each def pushes a one-command layer in
 * front of base, which holds the built-ins and then the host's commands.
 * Nodes live until close, since a running body may still use an earlier one;
 * an import unlinks a module's layers, so close frees from nodes. */
struct cmdr_session {
    cmdr head;
    cmdr base;
    cmdr_command builtins[9];
    int effects;
    FILE *out;
    definition *nodes;
    module **modules;
    size_t nmodules;
};

/* script-file, script-dir, and script-args start here in builtins. */
#define SCRIPT_BUILTINS 2

/* A zeroed node the session frees at close. */
static definition *new_node(cmdr_session *session)
{
    definition *node = cmdr_realloc(NULL, 1, sizeof(*node));
    *node = (definition){.next = session->nodes};
    session->nodes = node;
    return node;
}

static void push_node(cmdr_session *session, definition *node, size_t count)
{
    node->layer = (cmdr){node->commands, count, session->head.parent};
    session->head.parent = &node->layer;
}

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

/* PATH's directory, for a path from realpath: absolute, with no trailing '/'.
 * Not dirname(3), which may return storage shared between threads. */
static char *parent_dir(const char *path)
{
    size_t n = (size_t)(strrchr(path, '/') - path);
    return cmdr_strndup(path, n ? n : 1);
}

static int script_dir_command(const cmdr *repl, void *user, size_t argc,
                              const cmdr_value *argv, cmdr_value *result, char **error)
{
    const definition *node = script_of(user, "script-dir", argc, error);
    if (!node) return 1;
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = parent_dir(node->path)};
    return 0;
}

static int script_args_command(const cmdr *repl, void *user, size_t argc,
                               const cmdr_value *argv, cmdr_value *result, char **error)
{
    const definition *node = script_of(user, "script-args", argc, error);
    return !node || cmdr_clone(&node->args, result, error);
}

/* A def, an as binding, or an alias of either, which owns its strings. */
static int defined(const cmdr_command *command)
{
    return command->block || command->value;
}

static int script_command(const cmdr_command *command)
{
    return command->run == script_file_command || command->run == script_dir_command
        || command->run == script_args_command;
}

/* A command's usage for the overview. A definition's defaults, which
 * usage_of writes last as "[NAME=VALUE]", show as "...", so long parameter
 * names do not widen every row; `help NAME` shows them. */
static char *overview_usage(const cmdr_command *command)
{
    const char *usage = command->usage ? command->usage : command->name;
    const char *cut = command->block ? strstr(usage, " [") : NULL;
    size_t n = cut ? (size_t)(cut - usage) : strlen(usage);
    char *text = cmdr_realloc(NULL, n + sizeof(" ..."), 1);
    memcpy(text, usage, n);
    strcpy(text + n, cut ? " ..." : "");
    return text;
}

/* help NAME: the command's full usage, then its documentation. */
static int help_one(const cmdr *repl, const char *name, cmdr_value *result, char **error)
{
    const cmdr_command *command = cmdr_find(repl, name);
    if (!command) return cmdr_error(error, "help: unknown command: %s", name);
    const char *usage = command->usage ? command->usage : command->name;
    const char *doc = command->docstring ? command->docstring : "";
    size_t size = strlen(usage) + strlen(doc) + 3;
    char *text = cmdr_realloc(NULL, size, 1);
    snprintf(text, size, "%s\n%s\n", usage, doc);
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = text};
    return 0;
}

/* Return help as text so it composes, e.g. `help | filter list`. */
static int help_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                        cmdr_value *result, char **error)
{
    if (argc > 1) return cmdr_error(error, "usage: help [NAME]");
    if (argc) return help_one(repl, argv[0].text, result, error);
    /* Tables in lookup order, then definitions oldest first, which reverses
     * their lookup order. Each def or binding is a layer of one command. */
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
            int def = group->count && defined(&group->commands[0]);
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
                char *usage = overview_usage(command);
                if ((int)strlen(usage) > width) width = (int)strlen(usage);
                free(usage);
            }
        }
    free(groups);
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    if (!out) { free(active); return cmdr_error(error, "help: %s", strerror(errno)); }
    for (size_t i = 0; i < count; ++i) {
        char *usage = overview_usage(active[i]);
        fprintf(out, "%-*s  %s\n", width, usage, active[i]->docstring ? active[i]->docstring : "");
        free(usage);
    }
    fputs("Compose with |; the returned value becomes the next command's first argument.\n"
          "Quote spaces and pipes; { (a b) ... } is a block taking $a and $b.\n"
          "... marks parameters with defaults; help NAME shows them.\n", out);
    fclose(out);
    free(active);
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = text};
    return 0;
}

/* A name must be callable as one unquoted word; a leading '{' starts a block. */
static int valid_name(const char *name)
{
    if (!*name || strchr("$#{", *name)) return 0;
    for (const char *p = name; *p; ++p)
        if (isspace((unsigned char)*p) || strchr("|'\"\\", *p)) return 0;
    return 1;
}

/* Help syntax from the block: "NAME A [B=1]" for { (a b=1) ... }, "NAME VALUE"
 * for { ... }, which takes one piped input. The block's line compiled it, so
 * its parameter list is valid. */
static char *usage_of(const char *name, const char *block)
{
    const char *p = block;
    while (isspace((unsigned char)*p)) ++p;
    size_t n = strlen(name);
    /* Each parameter gains at most a space and two brackets. */
    char *usage = cmdr_realloc(NULL, n + 2 * strlen(p) + sizeof(" VALUE"), 1), *u = usage + n;
    memcpy(usage, name, n);
    if (*p != '(') {
        u += sprintf(u, " VALUE");
    } else {
        for (++p; *p && *p != ')';) {
            if (isspace((unsigned char)*p)) { ++p; continue; }
            const char *start = p;
            while (*p && !isspace((unsigned char)*p) && *p != ')' && *p != '=') ++p;
            int has_default = *p == '=';
            *u++ = ' ';
            if (has_default) *u++ = '[';
            for (const char *c = start; c < p; ++c) *u++ = (char)toupper((unsigned char)*c);
            if (has_default) {
                while (*p && !isspace((unsigned char)*p) && *p != ')') *u++ = *p++;
                *u++ = ']';
            }
        }
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
    cmdr_value block;
    if (cmdr_clone(&argv[1], &block, error)) { free(docstring); return 1; }
    definition *node = new_node(session);
    node->block = block;
    /* The block resolves names where its line compiled, before this layer,
     * so naming itself calls the command it shadows. */
    node->commands[0] = (cmdr_command){.name = cmdr_strndup(name, strlen(name)),
                                       .usage = usage_of(name, body),
                                       .docstring = docstring,
                                       .block = &node->block};
    push_node(session, node, 1);
    return 0;
}

/* as VALUE NAME [DOC]: NAME becomes a command returning VALUE, for lines
 * compiled after this one. No value, as def and Python's assignment. */
static int as_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                      cmdr_value *result, char **error)
{
    cmdr_session *session = user;
    if (argc < 2 || argc > 3 || argv[1].kind != CMDR_TEXT
        || (argc == 3 && argv[2].kind != CMDR_TEXT))
        return cmdr_error(error, "usage: as VALUE NAME [DOC]");
    const char *name = argv[1].text;
    if (!valid_name(name)) return cmdr_error(error, "as: invalid name '%s'", name);
    if (!strcmp(name, "def")) return cmdr_error(error, "as: cannot redefine def");
    if (argv[0].kind == CMDR_BLOCK) return cmdr_error(error, "as %s: VALUE is a block; use def", name);
    cmdr_value value;
    if (cmdr_clone(&argv[0], &value, error)) return 1;
    char *docstring;
    if (argc == 3) {
        docstring = cmdr_strndup(argv[2].text, strlen(argv[2].text));
    } else {
        char *shown = cmdr_describe(&value);
        docstring = cmdr_realloc(NULL, strlen(shown) + sizeof("as: "), 1);
        sprintf(docstring, "as: %s", shown);
        free(shown);
    }
    definition *node = new_node(session);
    node->value = value;
    node->commands[0] = (cmdr_command){.name = cmdr_strndup(name, strlen(name)),
                                       .usage = cmdr_strndup(name, strlen(name)),
                                       .docstring = docstring,
                                       .value = &node->value};
    push_node(session, node, 1);
    *result = (cmdr_value){0};
    return 0;
}

/* Open a script for reading, closed on exec, so programs it runs do not
 * inherit it. O_CLOEXEC at open, since another thread may spawn meanwhile. */
static FILE *open_script(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    FILE *file = fdopen(fd, "r");
    if (!file) {
        int saved = errno;
        close(fd);
        errno = saved;
    }
    return file;
}

/* Run a script's lines in this session, so its definitions remain. */
static int load_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                        cmdr_value *result, char **error)
{
    if (!argc) return cmdr_error(error, "usage: load PATH [ARG ...]");
    FILE *file = open_script(argv[0].text);
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

/* A module name is one path component without a dot. */
static int module_name(const char *name)
{
    return valid_name(name) && !strpbrk(name, "./");
}

static char *join_path(const char *dir, const char *a, const char *b)
{
    char *path = cmdr_realloc(NULL, strlen(dir) + strlen(a) + strlen(b) + 3, 1);
    sprintf(path, "%s/%s%s%s", dir, a, *b ? "/" : "", b);
    return path;
}

/* Append dir to the search list, and to searched for messages. */
static void add_dir(char ***dirs, size_t *count, char *dir)
{
    *dirs = cmdr_realloc(*dirs, *count + 1, sizeof(**dirs));
    (*dirs)[(*count)++] = dir;
}

/* The directories import searches, in order, for code compiled in repl. */
static char **search_dirs(const cmdr *repl, size_t *count)
{
    char **dirs = NULL;
    *count = 0;
    const cmdr_command *file = cmdr_find(repl, "script-file");
    const definition *script = file && file->run == script_file_command ? file->user : NULL;
    if (script && script->path) {
        add_dir(&dirs, count, parent_dir(script->path));
    } else {
        add_dir(&dirs, count, cmdr_strndup(".", 1));
    }
    const char *path = getenv("CMDR_PATH");
    for (const char *p = path; p && *p;) {
        const char *end = strchr(p, ':');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n) add_dir(&dirs, count, cmdr_strndup(p, n));
        p += n + (end != NULL);
    }
    const char *home = getenv("HOME");
    if (home && *home) add_dir(&dirs, count, join_path(home, ".cmdr", "lib"));
#ifdef CMDR_LIBDIR
    add_dir(&dirs, count, cmdr_strndup(CMDR_LIBDIR, strlen(CMDR_LIBDIR)));
#endif
    return dirs;
}

static int regular_file(const char *path)
{
    struct stat st;
    return !stat(path, &st) && S_ISREG(st.st_mode);
}

/* The resolved path of module name, or NULL with *error listing the search.
 * verb, import or from, starts messages. */
static char *find_module(const cmdr *repl, const char *verb, const char *name, char **error)
{
    size_t count;
    char **dirs = search_dirs(repl, &count), *found = NULL;
    char *file = cmdr_realloc(NULL, strlen(name) + sizeof(".cmdr"), 1);
    sprintf(file, "%s.cmdr", name);
    for (size_t i = 0; i < count && !found; ++i) {
        char *candidates[] = {join_path(dirs[i], name, "__init__.cmdr"), join_path(dirs[i], file, "")};
        for (size_t k = 0; k < 2; ++k) {
            char resolved[PATH_MAX];
            if (!found && regular_file(candidates[k]) && realpath(candidates[k], resolved))
                found = cmdr_strndup(resolved, strlen(resolved));
            free(candidates[k]);
        }
    }
    if (!found) {
        size_t n = 1;
        for (size_t i = 0; i < count; ++i) n += strlen(dirs[i]) + 1;
        char *searched = cmdr_realloc(NULL, n, 1);
        *searched = '\0';
        for (size_t i = 0; i < count; ++i) {
            if (i) strcat(searched, ":");
            strcat(searched, dirs[i]);
        }
        cmdr_error(error, "%s %s: not found in %s", verb, name, searched);
        free(searched);
    }
    for (size_t i = 0; i < count; ++i) free(dirs[i]);
    free(dirs);
    free(file);
    return found;
}

/* Run the module at path, record its definitions, and hide its names. */
static int run_module(cmdr_session *session, module *m, const char *verb, const char *name,
                      char **error)
{
    FILE *in = open_script(m->path);
    if (!in) return cmdr_error(error, "%s %s: %s: %s", verb, name, m->path, strerror(errno));
    const cmdr *before = session->head.parent;
    FILE *out = session->out;
    session->out = NULL; /* As in Python, a module's values are not printed. */
    char *inner;
    int status = cmdr_session_load_args(session, in, m->path, 0, NULL, &inner);
    session->out = out;
    fclose(in);
    /* Newest first, so the first definition of a name is its latest. */
    for (const cmdr *layer = session->head.parent; !status && layer != before; layer = layer->parent) {
        const definition *node = (const definition *)layer;
        /* Script layers and aliases own neither a block nor a value. */
        if (node->block.kind != CMDR_BLOCK && node->value.kind == CMDR_NONE) continue;
        size_t i = 0;
        while (i < m->count && strcmp(m->exports[i]->name, node->commands[0].name)) ++i;
        if (i < m->count) continue;
        m->exports = cmdr_realloc(m->exports, m->count + 1, sizeof(*m->exports));
        m->exports[m->count++] = &node->commands[0];
    }
    for (size_t i = 0; i < m->count / 2; ++i) { /* Definition order, for help. */
        const cmdr_command *t = m->exports[i];
        m->exports[i] = m->exports[m->count - 1 - i];
        m->exports[m->count - 1 - i] = t;
    }
    session->head.parent = before;
    if (status) cmdr_error(error, "%s %s: %s", verb, name, inner);
    free(inner);
    return status;
}

/* Push an alias layer that calls command as name; usage and name are owned. */
static void bind(cmdr_session *session, const cmdr_command *command, char *name, char *usage)
{
    const char *doc = command->docstring ? command->docstring : "";
    definition *node = new_node(session);
    node->commands[0] = (cmdr_command){.name = name, .usage = usage,
                                       .docstring = cmdr_strndup(doc, strlen(doc)),
                                       .block = command->block, .value = command->value};
    push_node(session, node, 1);
}

/* Push ALIAS.NAME for each of the module's definitions. */
static void bind_module(cmdr_session *session, const module *m, const char *alias)
{
    size_t n = strlen(alias);
    for (size_t i = 0; i < m->count; ++i) {
        const cmdr_command *command = m->exports[i];
        const char *usage = command->usage ? command->usage : command->name;
        char *name = cmdr_realloc(NULL, n + strlen(command->name) + 2, 1);
        char *qualified = cmdr_realloc(NULL, n + strlen(usage) + 2, 1);
        sprintf(name, "%s.%s", alias, command->name);
        sprintf(qualified, "%s.%s", alias, usage);
        bind(session, command, name, qualified);
    }
}

static void free_module(module *m)
{
    free(m->path);
    free(m->exports);
    free(m);
}

/* Find module name for code compiled in repl, running it on first use. */
static module *load_module(const cmdr *repl, cmdr_session *session, const char *verb,
                           const char *name, char **error)
{
    char *path = find_module(repl, verb, name, error);
    if (!path) return NULL;
    module *m = NULL;
    for (size_t i = 0; i < session->nmodules && !m; ++i)
        if (!strcmp(session->modules[i]->path, path)) m = session->modules[i];
    if (m && m->loading) {
        free(path);
        cmdr_error(error, "%s %s: circular import", verb, name);
        return NULL;
    }
    if (!m) {
        m = cmdr_realloc(NULL, 1, sizeof(*m));
        *m = (module){.path = path, .loading = 1};
        path = NULL;
        session->modules = cmdr_realloc(session->modules, session->nmodules + 1,
                                        sizeof(*session->modules));
        session->modules[session->nmodules++] = m;
        if (run_module(session, m, verb, name, error)) {
            /* Not recorded, so the next import runs the module again. */
            size_t i = 0;
            while (session->modules[i] != m) ++i;
            memmove(&session->modules[i], &session->modules[i + 1],
                    (--session->nmodules - i) * sizeof(*session->modules));
            free_module(m);
            return NULL;
        }
        m->loading = 0;
    }
    free(path);
    return m;
}

static int import_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                          cmdr_value *result, char **error)
{
    cmdr_session *session = user;
    if (!(argc == 1 || (argc == 3 && !strcmp(argv[1].text, "as"))))
        return cmdr_error(error, "usage: import NAME [as ALIAS]");
    const char *name = argv[0].text, *alias = argv[argc - 1].text;
    if (!module_name(name)) return cmdr_error(error, "import: invalid module name '%s'", name);
    if (!module_name(alias)) return cmdr_error(error, "import: invalid alias '%s'", alias);
    module *m = load_module(repl, session, "import", name, error);
    if (!m) return 1;
    bind_module(session, m, alias);
    return 0;
}

/* from NAME import DEF [as ALIAS] ...: bind each DEF unqualified, or as
 * ALIAS. Every name is checked before any is bound, and a name that already
 * resolves to another command is an error, since shadowing it would change
 * what later lines call without warning. */
static int from_command(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                        cmdr_value *result, char **error)
{
    cmdr_session *session = user;
    static const char usage[] = "usage: from NAME import DEF [as ALIAS] ...";
    if (argc < 3 || strcmp(argv[1].text, "import")) return cmdr_error(error, usage);
    const char *name = argv[0].text;
    if (!module_name(name)) return cmdr_error(error, "from: invalid module name '%s'", name);
    /* Pairs of DEF and the name it is bound as. */
    const char **pairs = cmdr_realloc(NULL, 2 * argc, sizeof(*pairs));
    size_t n = 0;
    int status = 0;
    for (size_t i = 2; i < argc && !status; ++n) {
        const char *def = argv[i].text, *alias = def;
        if (!strcmp(def, "as")) status = cmdr_error(error, usage);
        else if (i + 1 < argc && !strcmp(argv[i + 1].text, "as")) {
            if (i + 2 >= argc) status = cmdr_error(error, usage);
            else alias = argv[i + 2].text;
            i += 3;
        } else {
            ++i;
        }
        if (!status && !module_name(alias))
            status = cmdr_error(error, "from %s: invalid name '%s'", name, alias);
        pairs[2 * n] = def, pairs[2 * n + 1] = alias;
    }
    module *m = status ? NULL : load_module(repl, session, "from", name, error);
    if (!m) {
        free(pairs);
        return 1;
    }
    const cmdr_command **found = cmdr_realloc(NULL, n, sizeof(*found));
    const cmdr *names = cmdr_session_commands(session);
    for (size_t k = 0; k < n && !status; ++k) {
        const char *def = pairs[2 * k], *alias = pairs[2 * k + 1];
        found[k] = NULL;
        for (size_t i = 0; i < m->count && !found[k]; ++i)
            if (!strcmp(m->exports[i]->name, def)) found[k] = m->exports[i];
        const cmdr_command *existing = cmdr_find(names, alias);
        if (!found[k]) {
            status = cmdr_error(error, "from %s import %s: %s has no definition %s", name, def,
                                name, def);
        } else if (existing && existing->block != found[k]->block) {
            status = cmdr_error(error, "from %s import %s: %s already exists; bind it with as",
                                name, def, alias);
        }
        for (size_t j = 0; j < k && !status; ++j)
            if (!strcmp(pairs[2 * j + 1], alias))
                status = cmdr_error(error, "from %s import %s: %s is bound twice", name, def,
                                    alias);
    }
    for (size_t k = 0; k < n && !status; ++k) {
        const char *alias = pairs[2 * k + 1];
        const cmdr_command *existing = cmdr_find(names, alias);
        if (existing) continue; /* Already bound to this definition. */
        /* Usage begins with the definition's name; swap in the alias. */
        const cmdr_command *command = found[k];
        const char *u = command->usage ? command->usage : command->name;
        size_t len = strlen(command->name);
        const char *rest = !strncmp(u, command->name, len) ? u + len : "";
        char *bound_usage = cmdr_realloc(NULL, strlen(alias) + strlen(rest) + 1, 1);
        sprintf(bound_usage, "%s%s", alias, rest);
        bind(session, command, cmdr_strndup(alias, strlen(alias)), bound_usage);
    }
    free(found);
    free(pairs);
    return status;
}

cmdr_session *cmdr_session_open(const cmdr *commands, int effects, FILE *out)
{
    cmdr_session *s = cmdr_realloc(NULL, 1, sizeof(*s));
    *s = (cmdr_session){
        .builtins = {
            {.name = "help", .usage = "help [NAME]",
             .docstring = "Show commands, or NAME's full usage and documentation",
             .run = help_command, .text = 1},
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
            {.name = "as", .usage = "as VALUE NAME [DOC]",
             .docstring = "Bind VALUE to NAME, a command for later lines",
             .run = as_command, .user = s},
            {.name = "load", .usage = "load PATH [ARG ...]",
             .docstring = "Run a script in this session, with arguments; PATH is relative to the cwd",
             .run = load_command, .user = s, .text = 1},
            {.name = "import", .usage = "import NAME [as ALIAS]",
             .docstring = "Run module NAME once; bind its definitions as NAME.DEF",
             .run = import_command, .user = s, .text = 1},
            {.name = "from", .usage = "from NAME import DEF [as ALIAS] ...",
             .docstring = "Run module NAME once; bind each DEF unqualified, if the name is new",
             .run = from_command, .user = s, .text = 1},
        },
        .effects = effects,
        .out = out,
    };
    /* load, import, and from are last, so a session without effects omits them. */
    s->base = (cmdr){s->builtins, effects ? 9 : 6, commands};
    s->head.parent = &s->base;
    return s;
}

void cmdr_session_close(cmdr_session *session)
{
    if (!session) return;
    for (definition *node = session->nodes, *next; node; node = next) {
        next = node->next;
        if (defined(&node->commands[0])) { /* Script layers share the built-ins' strings. */
            free((char *)node->commands[0].name);
            free((char *)node->commands[0].usage);
            free((char *)node->commands[0].docstring);
        }
        free(node->path);
        cmdr_free(&node->args);
        cmdr_free(&node->block);
        cmdr_free(&node->value);
        free(node);
    }
    for (size_t i = 0; i < session->nmodules; ++i) free_module(session->modules[i]);
    free(session->modules);
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
    if (!end || (*pipeline)[end - 1] != '|' || blank_or_comment(*pipeline)) return 0;
    /* An even run of backslashes escapes itself, not the '|'. */
    size_t slashes = 0;
    while (slashes < end - 1 && (*pipeline)[end - 2 - slashes] == '\\') ++slashes;
    return slashes % 2 == 0;
}

/* Push a layer of the script commands, reading user's path and arguments. */
static definition *push_script(cmdr_session *session, const definition *user)
{
    definition *node = new_node(session);
    for (size_t i = 0; i < 3; ++i) {
        node->commands[i] = session->builtins[SCRIPT_BUILTINS + i];
        node->commands[i].user = (void *)user;
    }
    push_node(session, node, 3);
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
