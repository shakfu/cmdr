/* Facts about the system and this process, as Python's os module gives them. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

static int uname_field(int node, const char *name, size_t argc, cmdr_value *result, char **error)
{
    if (argc) return cmdr_error(error, "usage: %s", name);
    struct utsname u;
    if (uname(&u) < 0) return cmdr_error(error, "%s: %s", name, strerror(errno));
    const char *field = node ? u.nodename : u.sysname;
    *result = cmdr_text(field, strlen(field));
    return 0;
}
COMMAND(sysname_command) { return uname_field(0, "os.sysname", argc, result, error); }
COMMAND(hostname_command) { return uname_field(1, "os.hostname", argc, result, error); }

COMMAND(cwd_command)
{
    if (argc) return cmdr_error(error, "usage: os.cwd");
    for (size_t size = 256;; size *= 2) {
        char *path = cmdr_realloc(NULL, size, 1);
        if (getcwd(path, size)) {
            *result = (cmdr_value){.kind = CMDR_TEXT, .text = path};
            return 0;
        }
        free(path);
        if (errno != ERANGE) return cmdr_error(error, "os.cwd: %s", strerror(errno));
    }
}

COMMAND(pid_command)
{
    if (argc) return cmdr_error(error, "usage: os.pid");
    char text[32];
    snprintf(text, sizeof(text), "%lld", (long long)getpid());
    *result = cmdr_text(text, strlen(text));
    return 0;
}

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    TEXT("sysname", "sysname", "Return the operating system's name, such as Linux or Darwin", sysname_command),
    TEXT("hostname", "hostname", "Return this machine's network name", hostname_command),
    TEXT("cwd", "cwd", "Return the current working directory", cwd_command),
    TEXT("pid", "pid", "Return this process's id", pid_command),
};
const cmdr cmdr_ext_os = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
