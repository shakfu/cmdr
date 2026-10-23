#define _POSIX_C_SOURCE 200809L
/* Extensions: posix_spawn_file_actions_addchdir_np, syscall, and kqueue's types. */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include "cmdr.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <regex.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) \
    || defined(__DragonFly__)
#include <sys/event.h>
#define CMDR_KQUEUE
#elif defined(__linux__)
#include <poll.h>
#include <sys/syscall.h>
#ifdef SYS_pidfd_open
#define CMDR_PIDFD
#endif
#endif

/* glibc declares it only under _GNU_SOURCE, which would change other
 * declarations here; glibc 2.26 and musl take this value. */
#if defined(__linux__) && !defined(POSIX_SPAWN_SETSID)
#define POSIX_SPAWN_SETSID 0x80
#endif

extern char **environ;

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

typedef struct {
    char *data;
    size_t length, capacity;
} buffer;

static void put(buffer *b, const char *text, size_t n)
{
    if (b->length + n >= b->capacity) {
        b->data = cmdr_realloc(b->data, b->length + n + 1, 2);
        b->capacity = (b->length + n + 1) * 2;
    }
    memcpy(b->data + b->length, text, n);
    b->length += n;
    b->data[b->length] = '\0';
}

static int finish(buffer *b, cmdr_value *result)
{
    put(b, "", 0);
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = b->data};
    return 0;
}

/* NAME is the command reporting the error, or NULL. */
static int require_text(const char *name, const cmdr_value *v, char **error)
{
    if (v->kind == CMDR_TEXT) return 0;
    const char *kind = v->kind == CMDR_LIST ? "a list" : "a block";
    return name ? cmdr_error(error, "%s: expected text, got %s", name, kind)
                : cmdr_error(error, "expected text, got %s", kind);
}

int cmdr_natural(const cmdr_value *v, size_t *value, char **error)
{
    if (require_text(NULL, v, error)) return 1;
    const char *p = v->text;
    size_t n = 0;
    for (; *p; ++p) {
        if (*p < '0' || *p > '9' || n > (SIZE_MAX - (size_t)(*p - '0')) / 10) break;
        n = n * 10 + (size_t)(*p - '0');
    }
    if (*p || !*v->text) return cmdr_error(error, "invalid nonnegative integer: %s", v->text);
    *value = n;
    return 0;
}

/* Lists pass through; text splits into lines, a final newline ending the last. */
static int as_list(const char *name, const cmdr_value *v, cmdr_value *out, char **error)
{
    if (v->kind == CMDR_LIST) return cmdr_clone(v, out, error);
    if (require_text(name, v, error)) return 1;
    *out = (cmdr_value){.kind = CMDR_LIST};
    for (const char *p = v->text; *p;) {
        const char *end = strchr(p, '\n');
        cmdr_push(out, cmdr_text(p, end ? (size_t)(end - p) : strlen(p)));
        if (!end) break;
        p = end + 1;
    }
    return 0;
}

/* Text commands: cmdr_call guarantees text arguments. */

COMMAND(text_command)
{
    buffer b = {0};
    for (size_t i = 0; i < argc; ++i) {
        if (i) put(&b, " ", 1);
        put(&b, argv[i].text, strlen(argv[i].text));
    }
    return finish(&b, result);
}

static int convert(int (*to)(int), const char *name, size_t argc, const cmdr_value *argv,
                   cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: %s TEXT", name);
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    for (char *p = result->text; *p; ++p) *p = (char)to((unsigned char)*p);
    return 0;
}
COMMAND(upper_command) { return convert(toupper, "upper", argc, argv, result, error); }
COMMAND(lower_command) { return convert(tolower, "lower", argc, argv, result, error); }

COMMAND(trim_command)
{
    if (argc != 1) return cmdr_error(error, "usage: trim TEXT");
    const char *start = argv[0].text, *end = start + strlen(start);
    while (start < end && isspace((unsigned char)*start)) ++start;
    while (end > start && isspace((unsigned char)end[-1])) --end;
    *result = cmdr_text(start, (size_t)(end - start));
    return 0;
}

static int size_result(size_t n, cmdr_value *result)
{
    char text[3 * sizeof(size_t) + 1];
    snprintf(text, sizeof(text), "%zu", n);
    *result = cmdr_text(text, strlen(text));
    return 0;
}

COMMAND(length_command)
{
    if (argc != 1) return cmdr_error(error, "usage: length TEXT");
    return size_result(strlen(argv[0].text), result);
}

COMMAND(replace_command)
{
    if (argc != 3 || !argv[1].text[0])
        return cmdr_error(error, "usage: replace TEXT NONEMPTY_SEARCH REPLACEMENT");
    const char *p = argv[0].text, *search = argv[1].text, *match;
    buffer b = {0};
    while ((match = strstr(p, search))) {
        put(&b, p, (size_t)(match - p));
        put(&b, argv[2].text, strlen(argv[2].text));
        p = match + strlen(search);
    }
    put(&b, p, strlen(p));
    return finish(&b, result);
}

cmdr_value cmdr_number_value(double value)
{
    /* The fewest digits that read back as VALUE: 0.1 + 0.2 needs 17, 1.0 / 3 needs 16. */
    char text[64];
    if (value == 0) value = 0; /* -0 prints as 0, so it equals 0. */
    for (int digits = 15; digits <= 17; ++digits) {
        snprintf(text, sizeof(text), "%.*g", digits, value);
        if (strtod(text, NULL) == value) break;
    }
    return cmdr_text(text, strlen(text));
}

int cmdr_number(const char *text, double *value, char **error)
{
    /* Decimal only: strtod also skips leading space and reads hex. */
    const char *digits = text + (*text == '+' || *text == '-');
    int decimal = !isspace((unsigned char)*text)
        && !(digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'));
    char *end;
    /* Not ERANGE, which glibc also sets for finite subnormal results. */
    *value = strtod(text, &end);
    if (!decimal || end == text || *end || !isfinite(*value))
        return cmdr_error(error, "invalid finite number: %s", text);
    return 0;
}

static int arithmetic(char op, size_t argc, const cmdr_value *argv,
                      cmdr_value *result, char **error)
{
    int binary = op == '-' || op == '/';
    if (argc < 2 || (binary && argc != 2))
        return cmdr_error(error, "arithmetic expects %s numbers",
                          binary ? "exactly two" : "at least two");
    double value, operand;
    if (cmdr_number(argv[0].text, &value, error)) return 1;
    for (size_t i = 1; i < argc; ++i) {
        if (cmdr_number(argv[i].text, &operand, error)) return 1;
        if (op == '/' && operand == 0) return cmdr_error(error, "division by zero");
        value = op == '+' ? value + operand : op == '-' ? value - operand
              : op == '*' ? value * operand : value / operand;
        if (!isfinite(value)) return cmdr_error(error, "arithmetic overflow");
    }
    *result = cmdr_number_value(value);
    return 0;
}
COMMAND(add_command) { return arithmetic('+', argc, argv, result, error); }
COMMAND(sub_command) { return arithmetic('-', argc, argv, result, error); }
COMMAND(mul_command) { return arithmetic('*', argc, argv, result, error); }
COMMAND(div_command) { return arithmetic('/', argc, argv, result, error); }

enum comparison { LT, GT, LE, GE };
static int compare(enum comparison op, size_t argc, const cmdr_value *argv,
                   cmdr_value *result, char **error)
{
    static const char *names[] = {"lt", "gt", "le", "ge"};
    static const char *phrases[] = {"less than", "greater than", "at most", "at least"};
    if (argc != 2) return cmdr_error(error, "usage: %s NUMBER LIMIT", names[op]);
    double a, b;
    if (cmdr_number(argv[0].text, &a, error) || cmdr_number(argv[1].text, &b, error)) return 1;
    int holds = op == LT ? a < b : op == GT ? a > b : op == LE ? a <= b : a >= b;
    if (!holds)
        return cmdr_error(error, "%s: %s is not %s %s", names[op], argv[0].text,
                          phrases[op], argv[1].text), CMDR_MISMATCH;
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return 0;
}
COMMAND(lt_command) { return compare(LT, argc, argv, result, error); }
COMMAND(gt_command) { return compare(GT, argc, argv, result, error); }
COMMAND(le_command) { return compare(LE, argc, argv, result, error); }
COMMAND(ge_command) { return compare(GE, argc, argv, result, error); }

/* Read to the end of file; NUL bytes do not fit the text model. */
static int slurp(FILE *file, const char *what, cmdr_value *result, char **error)
{
    buffer b = {0};
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), file))) {
        if (memchr(chunk, '\0', n)) {
            free(b.data);
            return cmdr_error(error, "%s: binary data is not supported", what);
        }
        put(&b, chunk, n);
    }
    if (ferror(file)) { free(b.data); return cmdr_error(error, "%s: read failed", what); }
    return finish(&b, result);
}

COMMAND(read_command)
{
    if (argc != 1) return cmdr_error(error, "usage: read PATH");
    FILE *file = fopen(argv[0].text, "rb");
    if (!file) return cmdr_error(error, "read %s: %s", argv[0].text, strerror(errno));
    int status = slurp(file, argv[0].text, result, error);
    fclose(file);
    return status;
}

/* Temporary files above the standard descriptors, so the child's dup2 onto
 * 0 and 1 cannot close them when cmdr runs with stdin or stdout closed. */
static FILE *process_stream(void)
{
    FILE *file = tmpfile();
    if (!file) return NULL;
    int fd = fcntl(fileno(file), F_DUPFD_CLOEXEC, 3);
    int saved = errno;
    fclose(file);
    if (fd < 0) { errno = saved; return NULL; }
    file = fdopen(fd, "w+b");
    if (!file) { saved = errno; close(fd); errno = saved; }
    return file;
}

/* PATH opened with flags, above the standard descriptors as process_stream's
 * files are; appending, so a reader can follow it and out and err can share it. */
static FILE *named_stream(const char *path, int flags)
{
    int opened = open(path, flags | O_CLOEXEC, 0666);
    if (opened < 0) return NULL;
    int fd = fcntl(opened, F_DUPFD_CLOEXEC, 3), saved = errno;
    close(opened);
    if (fd < 0) { errno = saved; return NULL; }
    int mode = flags & O_ACCMODE;
    FILE *file = fdopen(fd, mode == O_RDONLY ? "rb" : mode == O_WRONLY ? "ab" : "r+b");
    if (!file) { saved = errno; close(fd); errno = saved; }
    return file;
}

enum { OUTPUT_FLAGS = O_RDWR | O_CREAT | O_TRUNC | O_APPEND };

/* How to run a program. capture returns STATUS STDOUT STDERR, as run-with
 * does, in place of failing on a nonzero status. */
typedef struct {
    const char *name; /* The command, for messages. */
    const char *input;
    const char *dir;  /* NULL inherits cmdr's directory. */
    const char *out, *err; /* Files for stdout and stderr; NULL for temporary ones. */
    double timeout;   /* Seconds; 0 waits without limit. */
    int group;        /* Own process group, signalled whole on a timeout. */
    int capture;
    int detach;       /* Own session, stdin from /dev/null, no output unless out or err. */
} process;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Atomic, not volatile sig_atomic_t: a signal handler sets it, and every
 * thread's cmdr_call reads it. */
static atomic_int interrupted;
/* A pipe that holds a byte while interrupted is set, so a thread waiting for
 * a child wakes on an interrupt from any thread. -1 until the first wait makes
 * it, since a signal handler cannot. Two ints, not one 64-bit pair, so the
 * handler's loads are lock-free on 32-bit targets too. The write end is
 * published first, so a set read end means both are. */
static atomic_int wake_read = -1, wake_write = -1;

static void drain(int fd)
{
    char bytes[16];
    while (read(fd, bytes, sizeof(bytes)) > 0) continue;
}

void cmdr_interrupt(int on)
{
    int saved = errno;
    if (on && !atomic_exchange(&interrupted, 1)) {
        /* After the flag: a wait that makes the pipe meanwhile sees the flag. */
        int fd = atomic_load(&wake_write);
        if (fd >= 0) (void)!write(fd, "", 1);
    } else if (!on && atomic_load(&interrupted)) {
        /* Drain before clearing, so a handler that sets the flag meanwhile
         * writes nothing to drain, or writes after. */
        int fd = atomic_load(&wake_read);
        if (fd >= 0) drain(fd);
        atomic_store(&interrupted, 0);
    }
    errno = saved;
}

int cmdr_interrupted(void)
{
    return interrupted;
}

static int make_pipe(int p[2])
{
#if defined(__linux__) && defined(SYS_pipe2)
    return (int)syscall(SYS_pipe2, p, O_CLOEXEC | O_NONBLOCK);
#else
    /* No pipe2 on macOS, so a spawn in another thread meanwhile may inherit
     * the pipe. */
    if (pipe(p)) return -1;
    for (int i = 0; i < 2; ++i) {
        fcntl(p[i], F_SETFD, FD_CLOEXEC);
        fcntl(p[i], F_SETFL, O_NONBLOCK);
    }
    return 0;
#endif
}

/* The wake pipe's read end, made on first use, and its write end in *w; -1 if
 * it cannot be made or another thread is still publishing it. */
static int wake_fds(int *w)
{
    int r = atomic_load(&wake_read), none = -1, p[2];
    if (r < 0 && !make_pipe(p)) {
        if (atomic_compare_exchange_strong(&wake_write, &none, p[1])) {
            atomic_store(&wake_read, p[0]);
        } else {
            close(p[0]);
            close(p[1]);
        }
        r = atomic_load(&wake_read);
    }
    *w = atomic_load(&wake_write);
    return r;
}

/* A descriptor that is ready when child exits or, if wake >= 0, wake is
 * readable; -1 where the platform has none. */
static int watch_exit(pid_t child, int wake)
{
#if defined(CMDR_KQUEUE)
    int kq = kqueue();
    if (kq < 0) return -1;
    struct kevent change;
    EV_SET(&change, child, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, NULL);
    int failed = kevent(kq, &change, 1, NULL, 0, NULL);
    EV_SET(&change, wake, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (!failed && wake >= 0) failed = kevent(kq, &change, 1, NULL, 0, NULL);
    if (failed) {
        close(kq);
        return -1;
    }
    return kq;
#elif defined(CMDR_PIDFD)
    return (int)syscall(SYS_pidfd_open, child, 0); /* Close-on-exec always. */
#else
    return -1;
#endif
}

/* Block on the n descriptors in watch, and on Linux wake, for at most
 * seconds; with none, sleep a 10 ms tick. Returns early on a signal. A kqueue
 * is one descriptor, holding every child and wake. */
static void block(const int *watch, size_t n, int wake, double seconds)
{
    if (seconds > 3600) seconds = 3600; /* Callers loop; this bounds the conversions. */
    if (!n && seconds > 0.01) seconds = 0.01;
#if defined(CMDR_KQUEUE)
    /* macOS can drop NOTE_EXIT for a child that exits as it is registered;
     * callers' waitpid then reaps it within a second. */
    if (n && seconds > 1) seconds = 1;
#endif
    struct timespec limit = {(time_t)seconds, (long)((seconds - (time_t)seconds) * 1e9)};
#if defined(CMDR_KQUEUE)
    struct kevent event;
    if (n) {
        kevent(watch[0], NULL, 0, &event, 1, &limit);
        return;
    }
#elif defined(CMDR_PIDFD)
    if (n) {
        struct pollfd *fds = cmdr_realloc(NULL, n + 1, sizeof(*fds));
        for (size_t i = 0; i < n; ++i) fds[i] = (struct pollfd){.fd = watch[i], .events = POLLIN};
        fds[n] = (struct pollfd){.fd = wake, .events = POLLIN};
        poll(fds, n + (wake >= 0), (int)ceil(seconds * 1000));
        free(fds);
        return;
    }
#endif
    nanosleep(&limit, NULL);
}

/* After a wait that watched wake: a byte without the flag, from a set racing
 * a clear in another thread, would make every wait spin. */
static void settle(int wake, int w)
{
    if (wake >= 0 && !cmdr_interrupted()) {
        drain(wake);
        if (cmdr_interrupted()) (void)!write(w, "", 1);
    }
}

/* Wait until child exits, limit seconds pass, or, if interruptible, an
 * interrupt arrives; return the pid or 0 if it is still running. Blocks on
 * the child's exit and the wake pipe where the platform allows; else polls. */
static pid_t wait_until(pid_t child, int *wait_status, double limit, int interruptible)
{
    int w = -1, wake = interruptible ? wake_fds(&w) : -1;
    /* Polls rather than miss an interrupt it has no pipe for. */
    int watch = interruptible && wake < 0 ? -1 : watch_exit(child, wake);
    double deadline = now() + limit;
    pid_t waited;
    for (;;) {
        waited = waitpid(child, wait_status, WNOHANG);
        if (waited > 0 || (waited < 0 && errno != EINTR)) break;
        /* One reading of the clock: a second, later one could go negative,
         * which poll takes as no timeout. */
        double left = deadline - now();
        if (left <= 0 || (interruptible && cmdr_interrupted())) {
            waited = 0;
            break;
        }
        block(&watch, watch >= 0, wake, left);
        settle(wake, w);
    }
    int saved = errno;
    if (watch >= 0) close(watch);
    errno = saved;
    return waited;
}

/* Wait for child. Past the timeout, or on an interrupt, send SIGTERM, then
 * SIGKILL after a second, and reap it: a child that ignores SIGINT must not
 * outlive cmdr. */
static pid_t wait_child(pid_t child, double timeout, int group, int *wait_status, int *timed_out)
{
    pid_t waited = wait_until(child, wait_status, timeout > 0 ? timeout : INFINITY, 1);
    if (waited > 0 || (waited < 0 && errno != EINTR)) return waited;
    *timed_out = !cmdr_interrupted();
    pid_t target = group ? -child : child;
    kill(target, SIGTERM);
    if (!(waited = wait_until(child, wait_status, 1, 0))) kill(target, SIGKILL);
    if (!waited)
        do waited = waitpid(child, wait_status, 0); while (waited < 0 && errno == EINTR);
    return waited;
}

static int add_dir(posix_spawn_file_actions_t *actions, const char *dir)
{
#if defined(CMDR_HAVE_ADDCHDIR)
    return posix_spawn_file_actions_addchdir(actions, dir);
#elif defined(CMDR_HAVE_ADDCHDIR_NP)
    return posix_spawn_file_actions_addchdir_np(actions, dir);
#else
    return ENOSYS;
#endif
}

/* A started program: its pid and its temporary files. */
typedef struct {
    pid_t child;
    FILE *in, *out, *err;
} spawned;

static void close_streams(spawned *s)
{
    if (s->in) fclose(s->in);
    if (s->out) fclose(s->out);
    if (s->err) fclose(s->err);
    s->in = s->out = s->err = NULL;
}

/* Start args as p says. Files rather than pipes: a child that writes before
 * reading cannot block. */
static int spawn_process(const process *p, char **args, spawned *s, char **error)
{
    *s = (spawned){0};
    const char *program = args[0];
    if (!program[0]) return cmdr_error(error, "expected an executable name");
    struct stat st;
    if (p->dir && stat(p->dir, &st))
        return cmdr_error(error, "%s: dir %s: %s", p->name, p->dir, strerror(errno));
    if (p->dir && !S_ISDIR(st.st_mode))
        return cmdr_error(error, "%s: dir %s: %s", p->name, p->dir, strerror(ENOTDIR));
    const char *failed_path = NULL;
    s->in = p->detach ? named_stream("/dev/null", O_RDONLY) : process_stream();
    if (s->in && p->out) s->out = named_stream(failed_path = p->out, OUTPUT_FLAGS);
    else if (s->in) s->out = p->detach ? named_stream("/dev/null", O_WRONLY) : process_stream();
    int want_err = p->capture || p->detach || p->err;
    if (s->out && p->err) s->err = named_stream(failed_path = p->err, OUTPUT_FLAGS);
    else if (s->out && want_err) {
        failed_path = NULL;
        s->err = p->detach ? named_stream("/dev/null", O_WRONLY) : process_stream();
    }
    size_t n = strlen(p->input);
    if (!s->in || !s->out || (want_err && !s->err)) {
        if (failed_path) cmdr_error(error, "%s: %s: %s", p->name, failed_path, strerror(errno));
        else cmdr_error(error, "%s: temporary file: %s", p->name, strerror(errno));
        goto failed;
    }
    if (!p->detach && (fwrite(p->input, 1, n, s->in) != n || fflush(s->in) || fseek(s->in, 0, SEEK_SET))) {
        cmdr_error(error, "%s: preparing stdin: %s", p->name, strerror(errno));
        goto failed;
    }
    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc) { cmdr_error(error, "%s: %s", p->name, strerror(rc)); goto failed; }
    rc = posix_spawn_file_actions_adddup2(&actions, fileno(s->in), STDIN_FILENO);
    if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, fileno(s->out), STDOUT_FILENO);
    if (!rc && s->err) rc = posix_spawn_file_actions_adddup2(&actions, fileno(s->err), STDERR_FILENO);
    if (!rc && p->dir && (rc = add_dir(&actions, p->dir)) == ENOSYS) {
        posix_spawn_file_actions_destroy(&actions);
        cmdr_error(error, "%s: dir is not supported on this platform", p->name);
        goto failed;
    }
    posix_spawnattr_t attr;
    int attr_rc = posix_spawnattr_init(&attr);
    if (!rc) rc = attr_rc;
    if (!rc && p->group) rc = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    if (!rc && p->group) rc = posix_spawnattr_setpgroup(&attr, 0);
#if defined(POSIX_SPAWN_SETSID)
    if (!rc && p->detach) rc = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
#else /* A new group at least, so the terminal's Ctrl-C does not reach it. */
    if (!rc && p->detach) rc = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    if (!rc && p->detach) rc = posix_spawnattr_setpgroup(&attr, 0);
#endif
    if (!rc) rc = posix_spawnp(&s->child, program, &actions, &attr, args, environ);
    if (!attr_rc) posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    if (rc) { cmdr_error(error, "%s %s: %s", p->name, program, strerror(rc)); goto failed; }
    return 0;
failed:
    close_streams(s);
    return 1;
}

/* The result of s's program, which exited with wait_status, or ran past its
 * timeout; closes s's files. */
static int collect(const process *p, const char *program, spawned *s, int wait_status,
                   int timed_out, cmdr_value *result, char **error)
{
    int status = 1;
    if (fseek(s->out, 0, SEEK_SET) || (s->err && fseek(s->err, 0, SEEK_SET))) {
        cmdr_error(error, "%s: reading output: %s", p->name, strerror(errno));
        goto done;
    }
    if (p->capture) {
        char code[16];
        if (timed_out) snprintf(code, sizeof(code), "timeout");
        else if (WIFSIGNALED(wait_status)) snprintf(code, sizeof(code), "%d", 128 + WTERMSIG(wait_status));
        else snprintf(code, sizeof(code), "%d", WEXITSTATUS(wait_status));
        cmdr_value stdout_text, stderr_text = {0};
        if (slurp(s->out, program, &stdout_text, error)) goto done;
        if (slurp(s->err, program, &stderr_text, error)) { cmdr_free(&stdout_text); goto done; }
        *result = (cmdr_value){.kind = CMDR_LIST};
        cmdr_push(result, cmdr_text(code, strlen(code)));
        cmdr_push(result, stdout_text);
        cmdr_push(result, stderr_text);
        status = 0;
    } else if (WIFSIGNALED(wait_status))
        cmdr_error(error, "run %s: terminated by signal %d", program, WTERMSIG(wait_status));
    else if (WEXITSTATUS(wait_status))
        cmdr_error(error, "run %s: exited with status %d", program, WEXITSTATUS(wait_status));
    else
        status = slurp(s->out, program, result, error);
done:
    close_streams(s);
    return status;
}

static int execute(const process *p, char **args, cmdr_value *result, char **error)
{
    spawned s;
    if (spawn_process(p, args, &s, error)) return 1;
    int wait_status, timed_out = 0;
    if (wait_child(s.child, p->timeout, p->group, &wait_status, &timed_out) < 0) {
        cmdr_error(error, "%s %s: wait: %s", p->name, args[0], strerror(errno));
        close_streams(&s);
        return 1;
    }
    if (cmdr_interrupted()) { /* Not a status: run-with would return it as data. */
        close_streams(&s);
        return cmdr_error(error, "%s %s: interrupted", p->name, args[0]);
    }
    return collect(p, args[0], &s, wait_status, timed_out, result, error);
}

/* Run argv as PROGRAM ARG ..., all text. */
static int run_text(const process *p, size_t argc, const cmdr_value *argv,
                    cmdr_value *result, char **error)
{
    char **args = cmdr_realloc(NULL, argc + 1, sizeof(*args));
    for (size_t i = 0; i < argc; ++i) args[i] = argv[i].text;
    args[argc] = NULL;
    int status = execute(p, args, result, error);
    free(args);
    return status;
}

COMMAND(run_command)
{
    if (!argc) return cmdr_error(error, "usage: run PROGRAM [ARG ...]");
    return run_text(&(process){.name = "run", .input = ""}, argc, argv, result, error);
}

COMMAND(run_in_command)
{
    if (argc < 2) return cmdr_error(error, "usage: run-in TEXT PROGRAM [ARG ...]");
    return run_text(&(process){.name = "run-in", .input = argv[0].text}, argc - 1, argv + 1, result, error);
}

/* Append value's text to args, flattening lists; 1 for a block. */
static int add_args(const char *name, const cmdr_value *value, char ***args, size_t *count,
                    char **error)
{
    if (value->kind != CMDR_LIST) {
        if (require_text(name, value, error)) return 1;
        *args = cmdr_realloc(*args, *count + 2, sizeof(**args));
        (*args)[(*count)++] = value->text;
        return 0;
    }
    for (size_t i = 0; i < value->count; ++i)
        if (add_args(name, &value->items[i], args, count, error)) return 1;
    return 0;
}

/* The options each command takes, as bits of parse_options's names. */
enum { RUN_OPTIONS = 0x3f, SPAWN_OPTIONS = 0x31 };

/* Set p from OPTIONS, a list of NAME VALUE pairs, of those in allowed; errors
 * name p->name. */
static int parse_options(const cmdr_value *options, process *p, int allowed, char **error)
{
    const char *who = p->name;
    if (options->count % 2) return cmdr_error(error, "%s: OPTIONS must be NAME VALUE pairs", who);
    int seen[6] = {0};
    static const char *names[] = {"dir", "stdin", "timeout", "group", "out", "err"};
    for (size_t i = 0; i < options->count; i += 2) {
        const cmdr_value *name = &options->items[i], *value = &options->items[i + 1];
        if (require_text(who, name, error) || require_text(who, value, error)) return 1;
        size_t k = 0;
        while (k < 6 && strcmp(name->text, names[k])) ++k;
        if (k == 6 || !(allowed & 1 << k)) return cmdr_error(error, "%s: unknown option %s", who, name->text);
        if (seen[k]++) return cmdr_error(error, "%s: option %s given twice", who, name->text);
        if (k == 0) p->dir = value->text;
        if (k == 1) p->input = value->text;
        if (k == 2 && cmdr_number(value->text, &p->timeout, error)) return 1;
        if (k == 2 && p->timeout <= 0) return cmdr_error(error, "%s: timeout must be positive", who);
        if (k == 3 && strcmp(value->text, "0") && strcmp(value->text, "1"))
            return cmdr_error(error, "%s: group must be 0 or 1", who);
        if (k == 3) p->group = value->text[0] == '1';
        if (k >= 4 && !value->text[0]) return cmdr_error(error, "%s: %s must be a path", who, name->text);
        if (k == 4) p->out = value->text;
        if (k == 5) p->err = value->text;
    }
    return 0;
}

/* OPTIONS is a list of NAME VALUE pairs. A list argument adds its items as
 * arguments, at any depth, so an empty list adds none. */
COMMAND(run_with_command)
{
    static const char usage[] = "usage: run-with OPTIONS PROGRAM [ARG ...]";
    if (argc < 2 || argv[0].kind != CMDR_LIST) return cmdr_error(error, usage);
    process p = {.name = "run-with", .input = "", .capture = 1};
    if (parse_options(&argv[0], &p, RUN_OPTIONS, error)) return 1;
    char **args = NULL;
    size_t count = 0;
    for (size_t i = 1; i < argc; ++i)
        if (add_args(p.name, &argv[i], &args, &count, error)) { free(args); return 1; }
    if (!count) return cmdr_error(error, usage);
    args[count] = NULL;
    int status = execute(&p, args, result, error);
    free(args);
    return status;
}

/* run-all runs at most this many jobs at once; each holds four descriptors. */
enum { MAX_RUNNING = 64 };

/* A run-all job: how to run it, and its state once started. */
typedef struct {
    process p;
    char **args;
    spawned s;
    int state;      /* 0 waiting, 1 running, 2 done. */
    int watch;      /* The child's pidfd, or -1. */
    double deadline, kill_at; /* kill_at is 0 until SIGTERM is sent. */
    int killed, timed_out;
    cmdr_value result;
} job;

/* Signal j's program, or its group. */
static void signal_job(const job *j, int sig)
{
    kill(j->p.group ? -j->s.child : j->s.child, sig);
}

/* Run jobs, at most width at once, setting each one's result. On an error or
 * an interrupt, start no more and stop the running ones, as wait_child does. */
static int run_jobs(job *jobs, size_t n, size_t width, char **error)
{
    int w = -1, wake = wake_fds(&w), status = 0, wake_watched = wake >= 0;
    int *watch = cmdr_realloc(NULL, width, sizeof(*watch));
    int kq = -1, ticking = 0; /* ticking: some child has no watch, so poll. */
#if defined(CMDR_KQUEUE)
    struct kevent change;
    kq = kqueue();
    EV_SET(&change, wake, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kq >= 0 && wake >= 0 && kevent(kq, &change, 1, NULL, 0, NULL)) wake_watched = 0;
    if (kq < 0) ticking = 1;
#endif
    size_t next = 0, running = 0;
    for (;;) {
        int stopping = status || cmdr_interrupted();
        for (; !stopping && running < width && next < n; ++next, ++running) {
            job *j = &jobs[next];
            if (spawn_process(&j->p, j->args, &j->s, error)) {
                status = stopping = 1;
                break;
            }
            j->state = 1;
            j->deadline = j->p.timeout > 0 ? now() + j->p.timeout : INFINITY;
#if defined(CMDR_KQUEUE)
            EV_SET(&change, j->s.child, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, NULL);
            if (kq >= 0 && kevent(kq, &change, 1, NULL, 0, NULL)) ticking = 1;
            j->watch = -1;
#else
            j->watch = watch_exit(j->s.child, -1);
            if (j->watch < 0) ticking = 1;
#endif
        }
        double t = now(), until = INFINITY;
        size_t watched = 0;
        for (size_t i = 0; i < next; ++i) {
            job *j = &jobs[i];
            if (j->state != 1) continue;
            int wait_status;
            pid_t waited = waitpid(j->s.child, &wait_status, WNOHANG);
            if (waited < 0 && errno == EINTR) waited = 0;
            if (waited) {
                if (j->watch >= 0) close(j->watch);
                j->state = 2;
                --running;
                if (waited < 0) {
                    if (!status) cmdr_error(error, "run-all %s: wait: %s", j->args[0], strerror(errno));
                    close_streams(&j->s);
                    status = 1;
                } else if (!status && !cmdr_interrupted()) {
                    status = collect(&j->p, j->args[0], &j->s, wait_status, j->timed_out,
                                     &j->result, error);
                } else {
                    close_streams(&j->s);
                }
                continue;
            }
            if (!j->kill_at && (status || cmdr_interrupted() || t >= j->deadline)) {
                j->timed_out = !status && !cmdr_interrupted();
                signal_job(j, SIGTERM);
                j->kill_at = t + 1;
            } else if (j->kill_at && !j->killed && t >= j->kill_at) {
                signal_job(j, SIGKILL);
                j->killed = 1;
            }
            double due = j->killed ? INFINITY : j->kill_at ? j->kill_at : j->deadline;
            if (due < until) until = due;
            if (j->watch >= 0) watch[watched++] = j->watch;
        }
        stopping = status || cmdr_interrupted();
        if (!running && (stopping || next == n)) break;
        if (running < width && next < n && !stopping) continue;
        /* Once interrupted, the wake pipe stays readable; stop watching it. */
        int use_wake = wake_watched && !cmdr_interrupted() ? wake : -1;
#if defined(CMDR_KQUEUE)
        if (kq >= 0 && wake_watched && use_wake < 0) {
            EV_SET(&change, wake, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            kevent(kq, &change, 1, NULL, 0, NULL);
            wake_watched = 0;
        }
        if (!ticking) watch[0] = kq, watched = 1;
#endif
        double left = until - now(); /* Negative would make poll wait forever. */
        block(watch, ticking ? 0 : watched, use_wake, left > 0 ? left : 0);
        settle(use_wake, w);
    }
    if (kq >= 0) close(kq);
    free(watch);
    if (!status && cmdr_interrupted()) status = cmdr_error(error, "run-all: interrupted");
    return status;
}

/* JOBS is a list of jobs, each OPTIONS PROGRAM [ARG ...] as run-with takes. */
COMMAND(run_all_command)
{
    static const char usage[] = "usage: run-all JOBS N";
    if (argc != 2 || argv[0].kind != CMDR_LIST) return cmdr_error(error, usage);
    double width;
    if (require_text("run-all", &argv[1], error) || cmdr_number(argv[1].text, &width, error)) return 1;
    if (width != floor(width) || width < 1 || width > MAX_RUNNING)
        return cmdr_error(error, "run-all: N must be a whole number from 1 to %d", MAX_RUNNING);
    size_t n = argv[0].count;
    job *jobs = cmdr_realloc(NULL, n ? n : 1, sizeof(*jobs));
    int status = 0;
    for (size_t i = 0; i < n; ++i) jobs[i] = (job){.p = {.name = "run-all", .input = "", .capture = 1}};
    for (size_t i = 0; i < n && !status; ++i) {
        const cmdr_value *spec = &argv[0].items[i];
        size_t count = 0;
        if (spec->kind != CMDR_LIST || spec->count < 2 || spec->items[0].kind != CMDR_LIST)
            status = cmdr_error(error, "run-all: job %zu is not OPTIONS PROGRAM [ARG ...]", i + 1);
        else if (!(status = parse_options(&spec->items[0], &jobs[i].p, RUN_OPTIONS, error)))
            for (size_t k = 1; k < spec->count && !status; ++k)
                status = add_args("run-all", &spec->items[k], &jobs[i].args, &count, error);
        if (!status && !count)
            status = cmdr_error(error, "run-all: job %zu is not OPTIONS PROGRAM [ARG ...]", i + 1);
        if (!status) jobs[i].args[count] = NULL;
    }
    if (!status) status = run_jobs(jobs, n, (size_t)width, error);
    if (!status) *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < n; ++i) {
        if (!status) cmdr_push(result, jobs[i].result);
        else cmdr_free(&jobs[i].result);
        free(jobs[i].args);
    }
    free(jobs);
    return status;
}

/* Programs spawn started, not yet reaped; a lock guards them, since sessions
 * may run in several threads. */
static atomic_flag detached_lock = ATOMIC_FLAG_INIT;
static pid_t *detached;
static size_t detached_count;

/* Reap the exited programs spawn started, then add child to them. Only those:
 * waitpid(-1) could reap a child that run-with waits for in another thread. */
static void track_detached(pid_t child)
{
    while (atomic_flag_test_and_set(&detached_lock)) continue;
    size_t kept = 0;
    for (size_t i = 0; i < detached_count; ++i) {
        pid_t waited = waitpid(detached[i], NULL, WNOHANG);
        if (!waited || (waited < 0 && errno == EINTR)) detached[kept++] = detached[i];
    }
    detached = cmdr_realloc(detached, kept + 1, sizeof(*detached));
    detached[kept++] = child;
    detached_count = kept;
    atomic_flag_clear(&detached_lock);
}

/* Start a program in its own session and return its pid, without waiting. */
COMMAND(spawn_command)
{
    static const char usage[] = "usage: spawn OPTIONS PROGRAM [ARG ...]";
    if (argc < 2 || argv[0].kind != CMDR_LIST) return cmdr_error(error, usage);
    process p = {.name = "spawn", .input = "", .detach = 1};
    if (parse_options(&argv[0], &p, SPAWN_OPTIONS, error)) return 1;
    char **args = NULL;
    size_t count = 0;
    for (size_t i = 1; i < argc; ++i)
        if (add_args(p.name, &argv[i], &args, &count, error)) { free(args); return 1; }
    if (!count) return cmdr_error(error, usage);
    args[count] = NULL;
    spawned s;
    int status = spawn_process(&p, args, &s, error);
    free(args);
    if (status) return 1;
    close_streams(&s);
    track_detached(s.child);
    char pid[24];
    snprintf(pid, sizeof(pid), "%ld", (long)s.child);
    *result = cmdr_text(pid, strlen(pid));
    return 0;
}

COMMAND(split_command)
{
    if (argc != 2) return cmdr_error(error, "usage: split TEXT SEPARATOR");
    if (!argv[1].text[0]) return cmdr_error(error, "split: separator must be nonempty");
    const char *p = argv[0].text, *sep = argv[1].text, *match;
    *result = (cmdr_value){.kind = CMDR_LIST};
    if (!*p) return 0;
    while ((match = strstr(p, sep))) {
        cmdr_push(result, cmdr_text(p, (size_t)(match - p)));
        p = match + strlen(sep);
    }
    cmdr_push(result, cmdr_text(p, strlen(p)));
    return 0;
}

/* Predicates return their input when they hold, else CMDR_MISMATCH. */

/* Fail with format, which describes a and then b as %s. */
static int mismatch(char **error, const char *format, const cmdr_value *first,
                    const cmdr_value *second)
{
    char *a = cmdr_describe(first), *b = cmdr_describe(second);
    cmdr_error(error, format, a, b);
    free(a);
    free(b);
    return CMDR_MISMATCH;
}

COMMAND(equals_command)
{
    if (argc != 2) return cmdr_error(error, "usage: equals TEXT EXPECTED");
    if (strcmp(argv[0].text, argv[1].text)) return mismatch(error, "equals: %s differs from %s", &argv[0], &argv[1]);
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return 0;
}

COMMAND(contains_command)
{
    if (argc != 2) return cmdr_error(error, "usage: contains TEXT SUBSTRING");
    if (!strstr(argv[0].text, argv[1].text)) return mismatch(error, "contains: %s not found in %s", &argv[1], &argv[0]);
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return 0;
}

/* POSIX extended regular expressions, matched bytewise in the C locale. */
COMMAND(matches_command)
{
    if (argc != 2) return cmdr_error(error, "usage: matches TEXT REGEX");
    regex_t re;
#ifdef REG_ENHANCED /* macOS: the word anchors \< \> \b \B, as glibc has. */
    int rc = regcomp(&re, argv[1].text, REG_EXTENDED | REG_ENHANCED | REG_NOSUB);
#else
    int rc = regcomp(&re, argv[1].text, REG_EXTENDED | REG_NOSUB);
#endif
    if (rc) {
        char message[128];
        regerror(rc, &re, message, sizeof(message));
        return cmdr_error(error, "matches: invalid regex '%s': %s", argv[1].text, message);
    }
    rc = regexec(&re, argv[0].text, 0, NULL, 0);
    regfree(&re);
    if (rc == REG_NOMATCH) return mismatch(error, "matches: %s does not match %s", &argv[0], &argv[1]);
    if (rc) return cmdr_error(error, "matches: regexec failed");
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return 0;
}

COMMAND(nonempty_command)
{
    if (argc != 1) return cmdr_error(error, "usage: nonempty VALUE");
    if (argv[0].kind == CMDR_TEXT && !argv[0].text[0])
        return cmdr_error(error, "nonempty: empty text"), CMDR_MISMATCH;
    if (argv[0].kind == CMDR_LIST && !argv[0].count)
        return cmdr_error(error, "nonempty: empty list"), CMDR_MISMATCH;
    return cmdr_clone(&argv[0], result, error);
}

static int bytes(int drop, size_t argc, const cmdr_value *argv,
                 cmdr_value *result, char **error)
{
    if (argc != 2) return cmdr_error(error, "usage: %s TEXT N", drop ? "drop" : "take");
    size_t n = strlen(argv[0].text), count;
    if (cmdr_natural(&argv[1], &count, error)) return 1;
    if (count > n) count = n;
    *result = drop ? cmdr_text(argv[0].text + count, n - count) : cmdr_text(argv[0].text, count);
    return 0;
}
COMMAND(take_command) { return bytes(0, argc, argv, result, error); }
COMMAND(drop_command) { return bytes(1, argc, argv, result, error); }

static int path_part(int directory, size_t argc, const cmdr_value *argv,
                     cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: %s PATH", directory ? "path-dirname" : "path-basename");
    const char *path = argv[0].text;
    size_t end = strlen(path), start;
    while (end > 1 && path[end - 1] == '/') --end;
    for (start = end; start && path[start - 1] != '/'; --start) {}
    if (end == 1 && path[0] == '/') *result = cmdr_text("/", 1);
    else if (!directory) *result = end ? cmdr_text(path + start, end - start) : cmdr_text(".", 1);
    else {
        while (start > 1 && path[start - 1] == '/') --start;
        *result = start ? cmdr_text(path, start) : cmdr_text(".", 1);
    }
    return 0;
}
COMMAND(basename_command) { return path_part(0, argc, argv, result, error); }
COMMAND(dirname_command) { return path_part(1, argc, argv, result, error); }

static int by_text(const void *a, const void *b)
{
    return strcmp(((const cmdr_value *)a)->text, ((const cmdr_value *)b)->text);
}

/* Items are checked numbers; equal values fall back to byte order. */
static int by_number(const void *a, const void *b)
{
    double x = strtod(((const cmdr_value *)a)->text, NULL);
    double y = strtod(((const cmdr_value *)b)->text, NULL);
    return x < y ? -1 : x > y ? 1 : by_text(a, b);
}

COMMAND(files_command)
{
    if (argc > 1) return cmdr_error(error, "usage: files [DIRECTORY]");
    const char *path = argc ? argv[0].text : ".";
    DIR *directory = opendir(path);
    if (!directory) return cmdr_error(error, "files %s: %s", path, strerror(errno));
    *result = (cmdr_value){.kind = CMDR_LIST};
    int status = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno) status = cmdr_error(error, "files %s: %s", path, strerror(errno));
            break;
        }
        struct stat info;
        if (fstatat(dirfd(directory), entry->d_name, &info, 0)) {
            if (errno == ENOENT) continue; /* A dangling link, or removed while listing. */
            status = cmdr_error(error, "files %s/%s: %s", path, entry->d_name, strerror(errno));
            break;
        }
        if (!S_ISREG(info.st_mode)) continue;
        buffer b = {0};
        put(&b, path, strlen(path));
        if (path[0] && path[strlen(path) - 1] != '/') put(&b, "/", 1);
        put(&b, entry->d_name, strlen(entry->d_name));
        cmdr_push(result, (cmdr_value){.kind = CMDR_TEXT, .text = b.data});
    }
    closedir(directory);
    if (!status && result->count) qsort(result->items, result->count, sizeof(*result->items), by_text);
    return status;
}

/* List commands: arguments may be lists or newline-separated text. */

COMMAND(list_command)
{
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < argc; ++i) {
        cmdr_value item;
        if (cmdr_clone(&argv[i], &item, error)) return 1;
        cmdr_push(result, item);
    }
    return 0;
}

COMMAND(push_command)
{
    if (!argc) return cmdr_error(error, "usage: push LIST_OR_LINES [VALUE ...]");
    if (as_list("push", &argv[0], result, error)) return 1;
    for (size_t i = 1; i < argc; ++i) {
        cmdr_value item;
        if (cmdr_clone(&argv[i], &item, error)) { cmdr_free(result); return 1; }
        cmdr_push(result, item);
    }
    return 0;
}

COMMAND(lines_command)
{
    if (argc != 1) return cmdr_error(error, "usage: lines LIST_OR_LINES");
    return as_list("lines", &argv[0], result, error);
}

COMMAND(join_command)
{
    if (argc != 2) return cmdr_error(error, "usage: join LIST_OR_LINES SEPARATOR");
    cmdr_value list;
    if (require_text("join", &argv[1], error) || as_list("join", &argv[0], &list, error)) return 1;
    buffer b = {0};
    int status = 0;
    for (size_t i = 0; i < list.count && !status; ++i) {
        if ((status = require_text("join", &list.items[i], error))) break;
        if (i) put(&b, argv[1].text, strlen(argv[1].text));
        put(&b, list.items[i].text, strlen(list.items[i].text));
    }
    cmdr_free(&list);
    if (status) { free(b.data); return 1; }
    return finish(&b, result);
}

/* Text input gives text output, one item per line; lists stay lists. Text
 * input splits into text items, so every item here is text. */
static int like_input(const cmdr_value *input, cmdr_value *result)
{
    if (input->kind != CMDR_TEXT) return 0;
    buffer b = {0};
    for (size_t i = 0; i < result->count; ++i) {
        put(&b, result->items[i].text, strlen(result->items[i].text));
        put(&b, "\n", 1);
    }
    cmdr_free(result);
    return finish(&b, result);
}

enum operation { SORT, SORT_N, UNIQ, HEAD, TAIL, FILTER, COUNT };
static int collection(enum operation op, size_t argc, const cmdr_value *argv,
                      cmdr_value *result, char **error)
{
    static const char *names[] = {"sort", "sort-n", "uniq", "head", "tail", "filter", "count"};
    int extra = op == HEAD || op == TAIL || op == FILTER;
    if (argc != (size_t)(extra ? 2 : 1))
        return cmdr_error(error, "usage: %s LIST_OR_LINES%s", names[op],
                          op == FILTER ? " SUBSTRING" : extra ? " N" : "");
    size_t limit = 0;
    if ((op == HEAD || op == TAIL) && cmdr_natural(&argv[1], &limit, error)) return 1;
    if (op == FILTER && require_text(names[op], &argv[1], error)) return 1;
    cmdr_value list;
    if (as_list(names[op], &argv[0], &list, error)) return 1;
    if (op == COUNT) {
        size_result(list.count, result);
        cmdr_free(&list);
        return 0;
    }
    double unused;
    for (size_t i = 0; op != HEAD && op != TAIL && i < list.count; ++i)
        if (require_text(names[op], &list.items[i], error)
            || (op == SORT_N && cmdr_number(list.items[i].text, &unused, error))) {
            cmdr_free(&list);
            return 1;
        }
    if (op == SORT && list.count) qsort(list.items, list.count, sizeof(*list.items), by_text);
    if (op == SORT_N && list.count) qsort(list.items, list.count, sizeof(*list.items), by_number);
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t i = 0; i < list.count; ++i) {
        cmdr_value *item = &list.items[i];
        if ((op == UNIQ && i && !strcmp(item->text, item[-1].text))
            || (op == HEAD && i >= limit)
            || (op == TAIL && limit < list.count && i < list.count - limit)
            || (op == FILTER && !strstr(item->text, argv[1].text))) continue;
        cmdr_value copy;
        cmdr_clone(item, &copy, error); /* Cannot fail: list was cloned. */
        cmdr_push(result, copy);
    }
    cmdr_free(&list);
    return like_input(&argv[0], result);
}
COMMAND(sort_command) { return collection(SORT, argc, argv, result, error); }
COMMAND(sort_n_command) { return collection(SORT_N, argc, argv, result, error); }
COMMAND(uniq_command) { return collection(UNIQ, argc, argv, result, error); }
COMMAND(head_command) { return collection(HEAD, argc, argv, result, error); }
COMMAND(tail_command) { return collection(TAIL, argc, argv, result, error); }
COMMAND(filter_command) { return collection(FILTER, argc, argv, result, error); }
COMMAND(count_command) { return collection(COUNT, argc, argv, result, error); }

COMMAND(map_command)
{
    if (argc < 2) return cmdr_error(error, "usage: map LIST_OR_LINES COMMAND [ARG ...]");
    if (require_text("map", &argv[1], error)) return 1;
    const char *name = argv[1].text;
    if (!cmdr_find(repl, name)) return cmdr_error(error, "unknown command: %s", name);
    cmdr_value list;
    if (as_list("map", &argv[0], &list, error)) return 1;
    cmdr_value *args = cmdr_realloc(NULL, argc - 1, sizeof(*args));
    for (size_t i = 2; i < argc; ++i) args[i - 1] = argv[i];
    *result = (cmdr_value){.kind = CMDR_LIST};
    int status = 0;
    for (size_t i = 0; i < list.count && !status; ++i) {
        args[0] = list.items[i];
        cmdr_value item;
        char *detail;
        status = cmdr_call(repl, name, argc - 1, args, &item, &detail);
        if (status || item.kind == CMDR_NONE)
            status = cmdr_error(error, "map item %zu (%s): %s", i + 1, name,
                                status ? detail : "command returned no value");
        else
            cmdr_push(result, item);
        free(detail);
    }
    free(args);
    cmdr_free(&list);
    return status;
}

/* Check a block's arity before running it, so a wrong argument count is
 * reported rather than counted as false. Its line checked names already. */
static int check_body(const char *name, const cmdr_value *body, size_t argc, char **error)
{
    if (body->kind != CMDR_BLOCK) return cmdr_error(error, "%s: expected a block { ... }", name);
    char *detail = NULL;
    if (!cmdr_block_arity(body, argc, &detail)) return 0;
    cmdr_error(error, "%s: %s", name, detail);
    free(detail);
    return 1;
}

/* Call block argv[1] with subject, then argv[2..]. */
static int run_body(const cmdr_value *subject, size_t argc,
                    const cmdr_value *argv, cmdr_value *out, char **detail)
{
    cmdr_value *args = cmdr_realloc(NULL, argc - 1, sizeof(*args));
    args[0] = *subject;
    for (size_t i = 2; i < argc; ++i) args[i - 1] = argv[i];
    int status = cmdr_run(&argv[1], argc - 1, args, out, detail);
    free(args);
    return status;
}

static int per_item(int keep, size_t argc, const cmdr_value *argv,
                    cmdr_value *result, char **error)
{
    const char *name = keep ? "keep" : "each";
    if (argc < 2) return cmdr_error(error, "usage: %s LIST_OR_LINES BLOCK [ARG ...]", name);
    cmdr_value list;
    if (check_body(name, &argv[1], argc - 1, error) || as_list(name, &argv[0], &list, error))
        return 1;
    *result = (cmdr_value){.kind = CMDR_LIST};
    int status = 0;
    for (size_t i = 0; i < list.count && !status; ++i) {
        cmdr_value item;
        char *detail;
        int failed = run_body(&list.items[i], argc, argv, &item, &detail);
        if (keep && failed == CMDR_MISMATCH) {
            /* Drop the item. */
        } else if (failed) {
            status = cmdr_error(error, "%s item %zu: %s", name, i + 1, detail);
        } else if (keep) {
            cmdr_push(result, list.items[i]);
            list.items[i] = (cmdr_value){0};
        } else if (item.kind == CMDR_NONE) {
            status = cmdr_error(error, "each item %zu: block returned no value", i + 1);
        } else {
            cmdr_push(result, item);
            item = (cmdr_value){0};
        }
        cmdr_free(&item);
        free(detail);
    }
    cmdr_free(&list);
    return !status && keep ? like_input(&argv[0], result) : status;
}
COMMAND(each_command) { return per_item(0, argc, argv, result, error); }
COMMAND(keep_command) { return per_item(1, argc, argv, result, error); }

COMMAND(not_command)
{
    if (argc < 2) return cmdr_error(error, "usage: not VALUE BLOCK [ARG ...]");
    if (check_body("not", &argv[1], argc - 1, error)) return 1;
    cmdr_value out;
    char *detail;
    int failed = run_body(&argv[0], argc, argv, &out, &detail);
    cmdr_free(&out);
    int status = failed;
    if (failed == CMDR_MISMATCH) {
        status = cmdr_clone(&argv[0], result, error);
    } else if (failed) {
        cmdr_error(error, "not: %s", detail);
    } else {
        status = mismatch(error, "not: %s satisfies %s", &argv[0], &argv[1]);
    }
    free(detail);
    return status;
}

COMMAND(with_command)
{
    if (argc < 2) return cmdr_error(error, "usage: with VALUE BLOCK [ARG ...]");
    if (check_body("with", &argv[1], argc - 1, error)) return 1;
    return run_body(&argv[0], argc, argv, result, error); /* Status passes through. */
}

/* A count larger than this is more likely a typo than a loop, as in num.seq. */
#define REPEAT_LIMIT 10000000

/* Call the block N times with argv[2..], so it takes no counter it must use. */
/* Text adds itself and a list its items, so list a b | concat and
 * concat "### " $f both work. */
COMMAND(concat_command)
{
    buffer b = {0};
    for (size_t i = 0; i < argc; ++i) {
        const cmdr_value *v = &argv[i];
        size_t n = v->kind == CMDR_LIST ? v->count : 1;
        for (size_t k = 0; k < n; ++k) {
            const cmdr_value *t = v->kind == CMDR_LIST ? &v->items[k] : v;
            if (t->kind != CMDR_TEXT) {
                free(b.data);
                return cmdr_error(error, "concat: argument %zu %s a %s", i + 1,
                                  v->kind == CMDR_LIST ? "holds" : "is",
                                  t->kind == CMDR_LIST ? "list" : "block");
            }
            put(&b, t->text, strlen(t->text));
        }
    }
    return finish(&b, result);
}

/* format TEMPLATE [ARG ...], as Python's str.format without specifications:
 * {} takes the next argument, {N} argument N from 0, {{ and }} are braces.
 * Unlike Python, an argument no field uses is an error. */
COMMAND(format_command)
{
    if (argc < 1) return cmdr_error(error, "usage: format TEMPLATE [ARG ...]");
    for (size_t i = 0; i < argc; ++i)
        if (argv[i].kind != CMDR_TEXT)
            return i ? cmdr_error(error, "format: argument %zu is a %s; join it first", i - 1,
                                  argv[i].kind == CMDR_LIST ? "list" : "block")
                     : cmdr_error(error, "format: TEMPLATE is not text");
    const char *t = argv[0].text;
    size_t args = argc - 1, next = 0;
    int automatic = 0, manual = 0;
    /* Arguments are numbered from 0, as {N} counts them. */
    unsigned char *used = memset(cmdr_realloc(NULL, args ? args : 1, 1), 0, args ? args : 1);
    buffer b = {0};
    int status = 0;
    for (const char *p = t; *p && !status;) {
        if ((p[0] == '{' && p[1] == '{') || (p[0] == '}' && p[1] == '}')) {
            put(&b, p, 1);
            p += 2;
        } else if (*p == '}') {
            status = cmdr_error(error, "format: a single '}' in the template; write }} for a brace");
        } else if (*p == '{') {
            const char *end = strchr(p, '}');
            size_t n;
            if (!end) {
                status = cmdr_error(error, "format: '{' without '}' in the template; write {{ for a brace");
                break;
            }
            if (end == p + 1) {
                automatic = 1;
                n = next++;
            } else {
                char *stop;
                errno = 0;
                unsigned long k = strtoul(p + 1, &stop, 10);
                if (stop != end || !isdigit((unsigned char)p[1]) || errno) {
                    status = cmdr_error(error, "format: invalid field '%.*s'; use {} or {N}",
                                        (int)(end - p + 1), p);
                    break;
                }
                manual = 1;
                n = (size_t)k;
            }
            if (automatic && manual) {
                status = cmdr_error(error, "format: cannot mix {} and {N}");
            } else if (n >= args) {
                status = cmdr_error(error, "format: field %zu has no argument; %zu given", n, args);
            } else {
                used[n] = 1;
                put(&b, argv[n + 1].text, strlen(argv[n + 1].text));
                p = end + 1;
            }
        } else {
            const char *q = p;
            while (*q && *q != '{' && *q != '}') ++q;
            put(&b, p, (size_t)(q - p));
            p = q;
        }
    }
    for (size_t i = 0; i < args && !status; ++i)
        if (!used[i]) status = cmdr_error(error, "format: argument %zu is unused", i);
    free(used);
    if (status) {
        free(b.data);
        return 1;
    }
    return finish(&b, result);
}

/* retry N [every SECONDS] BLOCK [ARG ...]: call BLOCK until a call succeeds,
 * at most N times, waiting SECONDS between attempts. */
COMMAND(retry_command)
{
    static const char usage[] = "usage: retry N [every SECONDS] BLOCK [ARG ...]";
    size_t n, at = 1;
    double seconds = 0;
    if (argc < 2) return cmdr_error(error, usage);
    if (cmdr_natural(&argv[0], &n, error)) return 1;
    if (!n || n > REPEAT_LIMIT) return cmdr_error(error, "retry: N %zu is not 1 to %d", n, REPEAT_LIMIT);
    if (argv[1].kind == CMDR_TEXT && !strcmp(argv[1].text, "every")) {
        if (argc < 4) return cmdr_error(error, usage);
        if (argv[2].kind != CMDR_TEXT || cmdr_number(argv[2].text, &seconds, error)) {
            if (!*error) cmdr_error(error, "retry: SECONDS is not a number");
            return 1;
        }
        if (seconds < 0) return cmdr_error(error, "retry: SECONDS %s is below 0", argv[2].text);
        at = 3;
    }
    const cmdr_value *body = &argv[at];
    size_t nargs = argc - at - 1;
    if (check_body("retry", body, nargs, error)) return 1;
    int status = 0;
    char *detail = NULL;
    for (size_t i = 0; i < n; ++i) {
        if (i && seconds > 0) { /* Between attempts, in slices so an interrupt ends it. */
            for (double left = seconds; left > 0 && !cmdr_interrupted(); left -= 0.05) {
                double d = left < 0.05 ? left : 0.05;
                struct timespec slice = {(time_t)d, (long)((d - (double)(time_t)d) * 1e9)};
                nanosleep(&slice, NULL);
            }
        }
        free(detail);
        detail = NULL;
        status = cmdr_run(body, nargs, argv + at + 1, result, &detail);
        if (!status) {
            free(detail);
            return 0;
        }
        if (cmdr_interrupted()) break;
    }
    if (cmdr_interrupted()) {
        free(detail);
        return cmdr_error(error, "interrupted");
    }
    /* The last failure, with its status, so a mismatch stays a mismatch. */
    cmdr_error(error, "retry: %zu attempt%s failed: %s", n, n == 1 ? "" : "s",
               detail ? detail : "failed");
    free(detail);
    return status;
}

COMMAND(repeat_command)
{
    if (argc < 2) return cmdr_error(error, "usage: repeat N BLOCK [ARG ...]");
    size_t n;
    if (cmdr_natural(&argv[0], &n, error) || check_body("repeat", &argv[1], argc - 2, error))
        return 1;
    if (n > REPEAT_LIMIT) return cmdr_error(error, "repeat: %zu exceeds %d", n, REPEAT_LIMIT);
    *result = (cmdr_value){.kind = CMDR_LIST};
    int status = 0;
    for (size_t i = 0; i < n && !status; ++i) {
        cmdr_value item;
        char *detail;
        if (cmdr_run(&argv[1], argc - 2, argv + 2, &item, &detail))
            status = cmdr_error(error, "repeat call %zu: %s", i + 1, detail);
        else if (item.kind == CMDR_NONE)
            status = cmdr_error(error, "repeat call %zu: block returned no value", i + 1);
        else {
            cmdr_push(result, item);
            item = (cmdr_value){0};
        }
        cmdr_free(&item);
        free(detail);
    }
    if (status) cmdr_free(result);
    return status;
}

COMMAND(fold_command)
{
    if (argc < 3) return cmdr_error(error, "usage: fold LIST_OR_LINES INIT BLOCK [ARG ...]");
    cmdr_value list;
    if (check_body("fold", &argv[2], argc - 1, error) || as_list("fold", &argv[0], &list, error))
        return 1;
    if (cmdr_clone(&argv[1], result, error)) { cmdr_free(&list); return 1; }
    /* The block takes the accumulator, the item, then argv[3..]. */
    cmdr_value *args = cmdr_realloc(NULL, argc - 1, sizeof(*args));
    for (size_t i = 3; i < argc; ++i) args[i - 1] = argv[i];
    int status = 0;
    for (size_t i = 0; i < list.count && !status; ++i) {
        args[0] = *result;
        args[1] = list.items[i];
        cmdr_value next;
        char *detail;
        if (cmdr_run(&argv[2], argc - 1, args, &next, &detail))
            status = cmdr_error(error, "fold item %zu: %s", i + 1, detail);
        else if (next.kind == CMDR_NONE)
            status = cmdr_error(error, "fold item %zu: block returned no value", i + 1);
        else {
            cmdr_free(result);
            *result = next;
        }
        free(detail);
    }
    free(args);
    cmdr_free(&list);
    return status;
}

COMMAND(try_command)
{
    if (argc != 2 && argc != 3) return cmdr_error(error, "usage: try [INPUT] BLOCK FALLBACK");
    const cmdr_value *body = &argv[argc - 2];
    if (check_body("try", body, argc - 2, error)) return 1;
    if (argv[argc - 1].kind == CMDR_BLOCK && check_body("try", &argv[argc - 1], 1, error)) return 1;
    char *detail;
    int failed = cmdr_run(body, argc - 2, argv, result, &detail);
    /* An interrupt stops the line; a fallback would let it continue. */
    if (failed && cmdr_interrupted()) {
        *error = detail;
        return 1;
    }
    if (!failed) {
        free(detail);
        return 0;
    }
    if (argv[argc - 1].kind != CMDR_BLOCK) {
        free(detail);
        return cmdr_clone(&argv[argc - 1], result, error);
    }
    /* A handler block receives the error message, as Python's except E as e. */
    cmdr_value message = cmdr_text(detail ? detail : "failed", strlen(detail ? detail : "failed"));
    free(detail);
    int status = cmdr_run(&argv[argc - 1], 1, &message, result, error);
    cmdr_free(&message);
    return status;
}

/* As try, but only a mismatch selects FALLBACK; errors propagate. */
COMMAND(or_command)
{
    if (argc != 2 && argc != 3) return cmdr_error(error, "usage: or [INPUT] BLOCK FALLBACK");
    const cmdr_value *body = &argv[argc - 2];
    if (check_body("or", body, argc - 2, error)) return 1;
    int failed = cmdr_run(body, argc - 2, argv, result, error);
    if (failed != CMDR_MISMATCH) return failed;
    free(*error);
    *error = NULL;
    return cmdr_clone(&argv[argc - 1], result, error);
}

/* As or, but a mismatch fails with MESSAGE in place of the block's own. */
/* The blocks of if's arguments from start: COND THEN pairs, as indexes of
 * their CONDs, and ELSE's index, or 0 if there is none. */
static int if_shape(size_t argc, const cmdr_value *argv, size_t start, size_t *conds,
                    size_t *n, size_t *otherwise, char **error)
{
    *n = *otherwise = 0;
    for (size_t i = start; i < argc;) {
        int first = i == start;
        const char *word = first ? NULL : argv[i].kind == CMDR_TEXT ? argv[i].text : "";
        if (!first && strcmp(word, "elif") && strcmp(word, "else")) {
            char *got = cmdr_describe(&argv[i]);
            cmdr_error(error, "if: expected elif or else, got %s", got);
            free(got);
            return 1;
        }
        size_t at = first ? i : i + 1, need = !first && !strcmp(word, "else") ? 1 : 2;
        for (size_t k = at; k < at + need; ++k)
            if (k >= argc || argv[k].kind != CMDR_BLOCK)
                return cmdr_error(error, "if: expected a block for %s",
                                  need == 1 ? "else" : k == at ? "COND" : "THEN");
        if (need == 1) {
            if (at + 1 != argc) return cmdr_error(error, "if: else must come last");
            *otherwise = at;
        } else {
            conds[(*n)++] = at;
        }
        i = at + need;
    }
    return 0;
}

/* if [VALUE] COND THEN [elif COND THEN ...] [else ELSE]: run the THEN of the
 * first COND that holds, else ELSE, each with VALUE. Only that branch runs.
 * An error in a COND propagates; with no else, the last mismatch is the
 * result. VALUE is present when the arguments only parse with it. */
COMMAND(if_command)
{
    static const char usage[] = "usage: if [VALUE] COND THEN [elif COND THEN ...] [else ELSE]";
    if (argc < 2) return cmdr_error(error, usage);
    size_t *conds = cmdr_realloc(NULL, argc, sizeof(*conds)), n, otherwise, start = 0;
    char *ignored = NULL;
    int bad = argv[0].kind == CMDR_BLOCK ? if_shape(argc, argv, 0, conds, &n, &otherwise, &ignored)
                                         : 1;
    if (bad) {
        free(ignored);
        start = 1;
        bad = if_shape(argc, argv, 1, conds, &n, &otherwise, error);
    }
    /* Every block's arity, before any runs, so a typo in a branch not taken fails. */
    for (size_t k = 0; !bad && k < n; ++k)
        bad = check_body("if", &argv[conds[k]], start, error)
            || check_body("if", &argv[conds[k] + 1], start, error);
    if (!bad && otherwise) bad = check_body("if", &argv[otherwise], start, error);
    if (bad) {
        free(conds);
        return 1;
    }
    char *mismatch = NULL;
    int status = CMDR_MISMATCH;
    for (size_t k = 0; k < n && status == CMDR_MISMATCH; ++k) {
        cmdr_value held;
        char *detail;
        int failed = cmdr_run(&argv[conds[k]], start, argv, &held, &detail);
        cmdr_free(&held);
        if (failed == CMDR_MISMATCH) {
            free(mismatch);
            mismatch = detail;
        } else if (failed) {
            free(mismatch);
            free(conds);
            *error = detail;
            return failed;
        } else {
            free(detail);
            status = cmdr_run(&argv[conds[k] + 1], start, argv, result, error);
            free(mismatch);
            free(conds);
            return status;
        }
    }
    if (otherwise) {
        free(mismatch);
        status = cmdr_run(&argv[otherwise], start, argv, result, error);
    } else {
        *error = mismatch;
    }
    free(conds);
    return status;
}

COMMAND(check_command)
{
    if (argc != 2 && argc != 3) return cmdr_error(error, "usage: check [INPUT] BLOCK MESSAGE");
    const cmdr_value *body = &argv[argc - 2];
    if (check_body("check", body, argc - 2, error) || require_text("check", &argv[argc - 1], error))
        return 1;
    int failed = cmdr_run(body, argc - 2, argv, result, error);
    if (failed != CMDR_MISMATCH) return failed;
    free(*error);
    return cmdr_error(error, "%s", argv[argc - 1].text), CMDR_MISMATCH;
}

static int save(const char *mode, const char *name, size_t argc, const cmdr_value *argv,
                cmdr_value *result, char **error)
{
    if (argc != 2) return cmdr_error(error, "usage: %s VALUE PATH", name);
    if (require_text(name, &argv[1], error) || cmdr_clone(&argv[0], result, error)) return 1;
    FILE *file = fopen(argv[1].text, mode);
    if (!file) return cmdr_error(error, "%s %s: %s", name, argv[1].text, strerror(errno));
    cmdr_render(&argv[0], file);
    int failed = ferror(file);
    if (fclose(file) || failed) return cmdr_error(error, "writing %s failed", argv[1].text);
    return 0;
}
COMMAND(write_command) { return save("wb", "write", argc, argv, result, error); }
COMMAND(append_command) { return save("ab", "append", argc, argv, result, error); }

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
#define VALUE(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f}
static const cmdr_command pure[] = {
    TEXT("text", "text [ARG ...]", "Join arguments with spaces", text_command),
    TEXT("upper", "upper TEXT", "Uppercase text", upper_command),
    TEXT("lower", "lower TEXT", "Lowercase text", lower_command),
    TEXT("trim", "trim TEXT", "Trim surrounding whitespace", trim_command),
    TEXT("length", "length TEXT", "Count bytes", length_command),
    TEXT("replace", "replace TEXT OLD NEW", "Replace all literal matches (OLD nonempty)", replace_command),
    TEXT("take", "take TEXT N", "Take the first N bytes", take_command),
    TEXT("drop", "drop TEXT N", "Drop the first N bytes", drop_command),
    TEXT("split", "split TEXT SEPARATOR", "Split into a list", split_command),
    TEXT("add", "add A B [NUMBER ...]", "Add numbers", add_command),
    TEXT("sub", "sub A B", "Subtract B from A", sub_command),
    TEXT("mul", "mul A B [NUMBER ...]", "Multiply numbers", mul_command),
    TEXT("div", "div A B", "Divide A by B", div_command),
    TEXT("lt", "lt NUMBER LIMIT", "Return NUMBER if less than LIMIT", lt_command),
    TEXT("gt", "gt NUMBER LIMIT", "Return NUMBER if greater than LIMIT", gt_command),
    TEXT("le", "le NUMBER LIMIT", "Return NUMBER if at most LIMIT", le_command),
    TEXT("ge", "ge NUMBER LIMIT", "Return NUMBER if at least LIMIT", ge_command),
    TEXT("equals", "equals TEXT EXPECTED", "Return TEXT if equal to EXPECTED", equals_command),
    TEXT("contains", "contains TEXT SUBSTRING", "Return TEXT if it contains SUBSTRING", contains_command),
    TEXT("matches", "matches TEXT REGEX", "Return TEXT if it matches the extended REGEX", matches_command),
    VALUE("nonempty", "nonempty VALUE", "Return VALUE unless empty text or list", nonempty_command),
    VALUE("list", "list [VALUE ...]", "Construct a list", list_command),
    VALUE("push", "push LIST_OR_LINES [VALUE ...]", "Append values to a list", push_command),
    VALUE("lines", "lines LIST_OR_LINES", "Split text into a list of lines", lines_command),
    VALUE("join", "join LIST_OR_LINES SEPARATOR", "Join text items", join_command),
    VALUE("sort", "sort LIST_OR_LINES", "Sort text items by byte order", sort_command),
    VALUE("sort-n", "sort-n LIST_OR_LINES", "Sort numbers in ascending order", sort_n_command),
    VALUE("uniq", "uniq LIST_OR_LINES", "Remove adjacent duplicate text items", uniq_command),
    VALUE("head", "head LIST_OR_LINES N", "Keep the first N items", head_command),
    VALUE("tail", "tail LIST_OR_LINES N", "Keep the last N items", tail_command),
    VALUE("filter", "filter LIST_OR_LINES TEXT", "Keep text items containing TEXT", filter_command),
    VALUE("count", "count LIST_OR_LINES", "Count items", count_command),
    VALUE("map", "map LIST_OR_LINES COMMAND [ARG ...]", "Apply a command to each item; COMMAND is looked up when map runs", map_command),
    VALUE("each", "each LIST_OR_LINES BLOCK [ARG ...]", "Call a block per item", each_command),
    VALUE("keep", "keep LIST_OR_LINES BLOCK [ARG ...]", "Keep items whose block succeeds", keep_command),
    VALUE("not", "not VALUE BLOCK [ARG ...]", "Return VALUE if the block mismatches", not_command),
    VALUE("with", "with VALUE BLOCK [ARG ...]", "Call a block with VALUE", with_command),
    VALUE("fold", "fold LIST_OR_LINES INIT BLOCK [ARG ...]", "Combine items, left to right", fold_command),
    VALUE("format", "format TEMPLATE [ARG ...]", "Fill {} and {N} in TEMPLATE with ARGs, as Python's str.format", format_command),
    VALUE("concat", "concat [VALUE ...]", "Join text and list items without a separator", concat_command),
    VALUE("retry", "retry N [every SECONDS] BLOCK [ARG ...]", "Call a block until it succeeds, at most N times", retry_command),
    VALUE("repeat", "repeat N BLOCK [ARG ...]", "Call a block N times with ARGs; list the results", repeat_command),
    VALUE("try", "try [INPUT] BLOCK FALLBACK", "Return FALLBACK, or a handler block's result for the error, if the block fails", try_command),
    VALUE("or", "or [INPUT] BLOCK FALLBACK", "Return FALLBACK if the block mismatches", or_command),
    VALUE("if", "if [VALUE] COND THEN [elif COND THEN ...] [else ELSE]",
          "Run THEN for the first COND that holds, else ELSE", if_command),
    VALUE("check", "check [INPUT] BLOCK MESSAGE", "Mismatch with MESSAGE if the block mismatches", check_command),
    TEXT("path-basename", "path-basename PATH", "Return the final path component", basename_command),
    TEXT("path-dirname", "path-dirname PATH", "Return the parent path", dirname_command),
};

/* Commands that read or write files or run programs. */
static const cmdr_command effects[] = {
    TEXT("read", "read PATH", "Read a text file", read_command),
    VALUE("write", "write VALUE PATH", "Replace a file, returning VALUE", write_command),
    VALUE("append", "append VALUE PATH", "Append to a file, returning VALUE", append_command),
    TEXT("files", "files [DIRECTORY]", "List regular files, sorted by path", files_command),
    TEXT("run", "run PROGRAM [ARG ...]", "Run an executable, returning stdout", run_command),
    TEXT("run-in", "run-in TEXT PROGRAM [ARG ...]", "Run with TEXT on stdin, returning stdout", run_in_command),
    VALUE("run-with", "run-with OPTIONS PROGRAM [ARG ...]", "Run with dir, stdin, timeout, group, out, and err; return STATUS STDOUT STDERR", run_with_command),
    VALUE("run-all", "run-all JOBS N", "Run jobs, each OPTIONS PROGRAM [ARG ...], N at once; return each one's STATUS STDOUT STDERR", run_all_command),
    VALUE("spawn", "spawn OPTIONS PROGRAM [ARG ...]", "Start a program in its own session with dir, out, and err; return its pid without waiting", spawn_command),
};

static const cmdr effect_table = {effects, sizeof(effects) / sizeof(effects[0]), NULL};
const cmdr cmdr_pure = {pure, sizeof(pure) / sizeof(pure[0]), NULL};
const cmdr cmdr_primitives = {pure, sizeof(pure) / sizeof(pure[0]), &effect_table};
