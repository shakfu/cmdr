/* Check and signal processes by id, such as those spawn starts. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE /* sysctl's process types. */
#include "cmdr.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

static int same(const cmdr_value *value, cmdr_value *result)
{
    *result = cmdr_text(value->text, strlen(value->text));
    return 0;
}

/* A decimal integer that fits pid_t. */
static int parse_pid(const char *name, const char *text, pid_t *pid, char **error)
{
    const char *digits = text + (*text == '-');
    char *end;
    errno = 0;
    long long n = strtoll(text, &end, 10);
    if (!*digits || strspn(digits, "0123456789") != strlen(digits) || errno || *end || (pid_t)n != n)
        return cmdr_error(error, "%s: PID is not an integer: %s", name, text);
    *pid = (pid_t)n;
    return 0;
}

/* Linux and macOS report a zombie, which kill(pid, 0) still finds: it has
 * exited, and waits for its parent to reap it. */
static int zombie(pid_t pid)
{
#if defined(__linux__)
    char path[64], line[512];
    snprintf(path, sizeof(path), "/proc/%lld/stat", (long long)pid);
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT;
    size_t n = fread(line, 1, sizeof(line) - 1, f);
    fclose(f);
    line[n] = '\0';
    const char *close = strrchr(line, ')'); /* The name may hold ") ". */
    return close && close[1] == ' ' && close[2] == 'Z';
#elif defined(__APPLE__)
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid};
    struct kinfo_proc info;
    size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, NULL, 0) || !size) return 0;
    return info.kp_proc.p_stat == SZOMB;
#else
    return 0;
#endif
}

COMMAND(alive_command)
{
    if (argc != 1) return cmdr_error(error, "usage: proc.alive PID");
    pid_t pid;
    if (parse_pid("proc.alive", argv[0].text, &pid, error)) return 1;
    if (pid <= 0) return cmdr_error(error, "proc.alive: PID must be positive: %s", argv[0].text);
    if (kill(pid, 0) && errno != EPERM) {
        if (errno != ESRCH) return cmdr_error(error, "proc.alive %s: %s", argv[0].text, strerror(errno));
        return cmdr_error(error, "proc.alive: no process %s", argv[0].text), CMDR_MISMATCH;
    }
    if (zombie(pid)) return cmdr_error(error, "proc.alive: process %s has exited", argv[0].text), CMDR_MISMATCH;
    return same(&argv[0], result);
}

static const struct {
    const char *name;
    int number;
} signals[] = {
    {"HUP", SIGHUP}, {"INT", SIGINT}, {"QUIT", SIGQUIT}, {"KILL", SIGKILL}, {"USR1", SIGUSR1},
    {"USR2", SIGUSR2}, {"PIPE", SIGPIPE}, {"ALRM", SIGALRM}, {"TERM", SIGTERM}, {"CHLD", SIGCHLD},
    {"CONT", SIGCONT}, {"STOP", SIGSTOP}, {"TSTP", SIGTSTP}, {"WINCH", SIGWINCH},
};

/* A name, with or without SIG, or a number. */
static int parse_signal(const char *text, int *number, char **error)
{
    const char *name = strncmp(text, "SIG", 3) ? text : text + 3;
    for (size_t i = 0; i < sizeof(signals) / sizeof(*signals); ++i)
        if (!strcmp(name, signals[i].name)) { *number = signals[i].number; return 0; }
    char *end;
    errno = 0;
    long n = strtol(text, &end, 10);
    if (*text >= '0' && *text <= '9' && !*end && !errno && n < 128) { *number = (int)n; return 0; }
    return cmdr_error(error, "proc.kill: unknown signal %s", text);
}

/* As kill(2): a negative PID names a process group. 0 and -1, which name
 * cmdr's own group and every process, are refused. */
COMMAND(kill_command)
{
    if (argc < 1 || argc > 2) return cmdr_error(error, "usage: proc.kill PID [SIGNAL]");
    pid_t pid;
    int number = SIGTERM;
    if (parse_pid("proc.kill", argv[0].text, &pid, error)) return 1;
    if (pid == 0 || pid == -1) return cmdr_error(error, "proc.kill: refusing PID %s", argv[0].text);
    if (argc == 2 && parse_signal(argv[1].text, &number, error)) return 1;
    if (kill(pid, number)) {
        if (errno != ESRCH) return cmdr_error(error, "proc.kill %s: %s", argv[0].text, strerror(errno));
        return cmdr_error(error, "proc.kill: no process %s", argv[0].text), CMDR_MISMATCH;
    }
    return same(&argv[0], result);
}

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    TEXT("alive", "alive PID", "Return PID if its process is running", alive_command),
    TEXT("kill", "kill PID [SIGNAL]", "Send SIGNAL, default TERM, to a process or, for -PID, a group", kill_command),
};
const cmdr cmdr_ext_proc = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
