#define _POSIX_C_SOURCE 200809L
#ifdef CMDR_HAVE_ADDCHDIR_NP
#define _DEFAULT_SOURCE /* glibc and musl declare it as an extension. */
#define _DARWIN_C_SOURCE
#endif
#include "cmdr.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <regex.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

static int require_text(const cmdr_value *v, char **error)
{
    return v->kind == CMDR_TEXT ? 0 : cmdr_error(error, "expected text");
}

int cmdr_natural(const cmdr_value *v, size_t *value, char **error)
{
    if (require_text(v, error)) return 1;
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
static int as_list(const cmdr_value *v, cmdr_value *out, char **error)
{
    if (v->kind == CMDR_LIST) return cmdr_clone(v, out, error);
    if (require_text(v, error)) return 1;
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
    char text[64];
    snprintf(text, sizeof(text), "%.17g", value);
    return cmdr_text(text, strlen(text));
}

int cmdr_number(const char *text, double *value, char **error)
{
    char *end;
    errno = 0;
    *value = strtod(text, &end);
    if (end == text || *end || errno == ERANGE || !isfinite(*value))
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

/* How to run a program. capture returns STATUS STDOUT STDERR, as run-with
 * does, in place of failing on a nonzero status. */
typedef struct {
    const char *name; /* The command, for messages. */
    const char *input;
    const char *dir;  /* NULL inherits cmdr's directory. */
    double timeout;   /* Seconds; 0 waits without limit. */
    int capture;
} process;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Poll until child exits or limit seconds pass; return the pid or 0 if it is
 * still running. */
static pid_t wait_until(pid_t child, int *wait_status, double limit)
{
    struct timespec tick = {0, 10 * 1000 * 1000};
    double deadline = now() + limit;
    for (;;) {
        pid_t waited = waitpid(child, wait_status, WNOHANG);
        if (waited > 0 || (waited < 0 && errno != EINTR)) return waited;
        if (now() >= deadline) return 0;
        nanosleep(&tick, NULL);
    }
}

/* Wait for child; past the timeout, send SIGTERM, then SIGKILL after a second. */
static pid_t wait_child(pid_t child, double timeout, int *wait_status, int *timed_out)
{
    pid_t waited = 0;
    if (timeout > 0 && !(waited = wait_until(child, wait_status, timeout))) {
        *timed_out = 1;
        kill(child, SIGTERM);
        if (!(waited = wait_until(child, wait_status, 1))) kill(child, SIGKILL);
    }
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

/* Files rather than pipes: a child that writes before reading cannot block. */
static int execute(const process *p, char **args, cmdr_value *result, char **error)
{
    const char *program = args[0];
    if (!program[0]) return cmdr_error(error, "expected an executable name");
    struct stat st;
    if (p->dir && stat(p->dir, &st))
        return cmdr_error(error, "run-with: dir %s: %s", p->dir, strerror(errno));
    if (p->dir && !S_ISDIR(st.st_mode))
        return cmdr_error(error, "run-with: dir %s: %s", p->dir, strerror(ENOTDIR));
    FILE *in = process_stream(), *out = in ? process_stream() : NULL;
    FILE *err = out && p->capture ? process_stream() : NULL;
    int status = 1;
    size_t n = strlen(p->input);
    if (!in || !out || (p->capture && !err)) {
        cmdr_error(error, "%s: temporary file: %s", p->name, strerror(errno));
        goto done;
    }
    if (fwrite(p->input, 1, n, in) != n || fflush(in) || fseek(in, 0, SEEK_SET)) {
        cmdr_error(error, "%s: preparing stdin: %s", p->name, strerror(errno));
        goto done;
    }
    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc) { cmdr_error(error, "%s: %s", p->name, strerror(rc)); goto done; }
    rc = posix_spawn_file_actions_adddup2(&actions, fileno(in), STDIN_FILENO);
    if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, fileno(out), STDOUT_FILENO);
    if (!rc && err) rc = posix_spawn_file_actions_adddup2(&actions, fileno(err), STDERR_FILENO);
    if (!rc && p->dir && (rc = add_dir(&actions, p->dir)) == ENOSYS) {
        posix_spawn_file_actions_destroy(&actions);
        cmdr_error(error, "run-with: dir is not supported on this platform");
        goto done;
    }
    pid_t child;
    if (!rc) rc = posix_spawnp(&child, program, &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc) { cmdr_error(error, "%s %s: %s", p->name, program, strerror(rc)); goto done; }
    int wait_status, timed_out = 0;
    if (wait_child(child, p->timeout, &wait_status, &timed_out) < 0) {
        cmdr_error(error, "%s %s: wait: %s", p->name, program, strerror(errno));
        goto done;
    }
    if (fseek(out, 0, SEEK_SET) || (err && fseek(err, 0, SEEK_SET))) {
        cmdr_error(error, "%s: reading output: %s", p->name, strerror(errno));
        goto done;
    }
    if (p->capture) {
        char code[16];
        if (timed_out) snprintf(code, sizeof(code), "timeout");
        else if (WIFSIGNALED(wait_status)) snprintf(code, sizeof(code), "%d", 128 + WTERMSIG(wait_status));
        else snprintf(code, sizeof(code), "%d", WEXITSTATUS(wait_status));
        cmdr_value stdout_text, stderr_text = {0};
        if (slurp(out, program, &stdout_text, error)) goto done;
        if (slurp(err, program, &stderr_text, error)) { cmdr_free(&stdout_text); goto done; }
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
        status = slurp(out, program, result, error);
done:
    if (in) fclose(in);
    if (out) fclose(out);
    if (err) fclose(err);
    return status;
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
static int add_args(const cmdr_value *value, char ***args, size_t *count, char **error)
{
    if (value->kind != CMDR_LIST) {
        if (require_text(value, error)) return 1;
        *args = cmdr_realloc(*args, *count + 2, sizeof(**args));
        (*args)[(*count)++] = value->text;
        return 0;
    }
    for (size_t i = 0; i < value->count; ++i)
        if (add_args(&value->items[i], args, count, error)) return 1;
    return 0;
}

/* OPTIONS is a list of NAME VALUE pairs. A list argument adds its items as
 * arguments, at any depth, so an empty list adds none. */
COMMAND(run_with_command)
{
    static const char usage[] = "usage: run-with OPTIONS PROGRAM [ARG ...]";
    if (argc < 2 || argv[0].kind != CMDR_LIST) return cmdr_error(error, usage);
    const cmdr_value *options = &argv[0];
    if (options->count % 2) return cmdr_error(error, "run-with: OPTIONS must be NAME VALUE pairs");
    process p = {.name = "run-with", .input = "", .capture = 1};
    int seen[3] = {0};
    static const char *names[] = {"dir", "stdin", "timeout"};
    for (size_t i = 0; i < options->count; i += 2) {
        const cmdr_value *name = &options->items[i], *value = &options->items[i + 1];
        if (require_text(name, error) || require_text(value, error)) return 1;
        size_t k = 0;
        while (k < 3 && strcmp(name->text, names[k])) ++k;
        if (k == 3) return cmdr_error(error, "run-with: unknown option %s", name->text);
        if (seen[k]++) return cmdr_error(error, "run-with: option %s given twice", name->text);
        if (k == 0) p.dir = value->text;
        if (k == 1) p.input = value->text;
        if (k == 2 && cmdr_number(value->text, &p.timeout, error)) return 1;
        if (k == 2 && p.timeout <= 0) return cmdr_error(error, "run-with: timeout must be positive");
    }
    char **args = NULL;
    size_t count = 0;
    for (size_t i = 1; i < argc; ++i)
        if (add_args(&argv[i], &args, &count, error)) { free(args); return 1; }
    if (!count) return cmdr_error(error, usage);
    args[count] = NULL;
    int status = execute(&p, args, result, error);
    free(args);
    return status;
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
    int rc = regcomp(&re, argv[1].text, REG_EXTENDED | REG_NOSUB);
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
    if (as_list(&argv[0], result, error)) return 1;
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
    return as_list(&argv[0], result, error);
}

COMMAND(join_command)
{
    if (argc != 2) return cmdr_error(error, "usage: join LIST_OR_LINES SEPARATOR");
    cmdr_value list;
    if (require_text(&argv[1], error) || as_list(&argv[0], &list, error)) return 1;
    buffer b = {0};
    int status = 0;
    for (size_t i = 0; i < list.count && !status; ++i) {
        if ((status = require_text(&list.items[i], error))) break;
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
    if (op == FILTER && require_text(&argv[1], error)) return 1;
    cmdr_value list;
    if (as_list(&argv[0], &list, error)) return 1;
    if (op == COUNT) {
        size_result(list.count, result);
        cmdr_free(&list);
        return 0;
    }
    double unused;
    for (size_t i = 0; op != HEAD && op != TAIL && i < list.count; ++i)
        if (require_text(&list.items[i], error)
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
    if (require_text(&argv[1], error)) return 1;
    const char *name = argv[1].text;
    if (!cmdr_find(repl, name)) return cmdr_error(error, "unknown command: %s", name);
    cmdr_value list;
    if (as_list(&argv[0], &list, error)) return 1;
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
    if (check_body(name, &argv[1], argc - 1, error) || as_list(&argv[0], &list, error))
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

COMMAND(fold_command)
{
    if (argc < 3) return cmdr_error(error, "usage: fold LIST_OR_LINES INIT BLOCK [ARG ...]");
    cmdr_value list;
    if (check_body("fold", &argv[2], argc - 1, error) || as_list(&argv[0], &list, error))
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
    char *detail;
    int failed = cmdr_run(body, argc - 2, argv, result, &detail);
    free(detail);
    return failed ? cmdr_clone(&argv[argc - 1], result, error) : 0;
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
COMMAND(check_command)
{
    if (argc != 2 && argc != 3) return cmdr_error(error, "usage: check [INPUT] BLOCK MESSAGE");
    const cmdr_value *body = &argv[argc - 2];
    if (check_body("check", body, argc - 2, error) || require_text(&argv[argc - 1], error))
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
    if (require_text(&argv[1], error) || cmdr_clone(&argv[0], result, error)) return 1;
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
    VALUE("map", "map LIST_OR_LINES COMMAND [ARG ...]", "Apply a command to each item", map_command),
    VALUE("each", "each LIST_OR_LINES BLOCK [ARG ...]", "Call a block per item", each_command),
    VALUE("keep", "keep LIST_OR_LINES BLOCK [ARG ...]", "Keep items whose block succeeds", keep_command),
    VALUE("not", "not VALUE BLOCK [ARG ...]", "Return VALUE if the block mismatches", not_command),
    VALUE("with", "with VALUE BLOCK [ARG ...]", "Call a block with VALUE", with_command),
    VALUE("fold", "fold LIST_OR_LINES INIT BLOCK [ARG ...]", "Combine items, left to right", fold_command),
    VALUE("try", "try [INPUT] BLOCK FALLBACK", "Return FALLBACK if the block fails", try_command),
    VALUE("or", "or [INPUT] BLOCK FALLBACK", "Return FALLBACK if the block mismatches", or_command),
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
    VALUE("run-with", "run-with OPTIONS PROGRAM [ARG ...]", "Run with dir, stdin, and timeout; return STATUS STDOUT STDERR", run_with_command),
};

static const cmdr effect_table = {effects, sizeof(effects) / sizeof(effects[0]), NULL};
const cmdr cmdr_pure = {pure, sizeof(pure) / sizeof(pure[0]), NULL};
const cmdr cmdr_primitives = {pure, sizeof(pure) / sizeof(pure[0]), &effect_table};
