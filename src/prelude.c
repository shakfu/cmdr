#define _POSIX_C_SOURCE 200809L
#include "prelude.h"
#include "essentials.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int text_command(void *user, size_t argc, const char *const *argv,
                        char **result, char **error)
{
    (void)user;
    size_t size = 1;
    for (size_t i = 0; i < argc; ++i) {
        size_t n = strlen(argv[i]);
        if (size > SIZE_MAX - (i != 0) || n > SIZE_MAX - size - (i != 0))
            return cmdr_error(error, "value is too large");
        size += n + (i != 0);
    }
    *result = malloc(size);
    if (!*result) return cmdr_error(error, "out of memory");
    char *p = *result;
    for (size_t i = 0; i < argc; ++i) {
        if (i) *p++ = ' ';
        size_t n = strlen(argv[i]);
        memcpy(p, argv[i], n);
        p += n;
    }
    *p = '\0';
    return 0;
}

static int upper_command(void *user, size_t argc, const char *const *argv,
                         char **result, char **error)
{
    (void)user;
    if (argc != 1) return cmdr_error(error, "usage: upper TEXT");
    *result = strdup(argv[0]);
    if (!*result) return cmdr_error(error, "out of memory");
    for (char *p = *result; *p; ++p) *p = (char)toupper((unsigned char)*p);
    return 0;
}

static int read_command(void *user, size_t argc, const char *const *argv,
                        char **result, char **error)
{
    (void)user;
    if (argc != 1) return cmdr_error(error, "usage: read PATH");
    FILE *file = fopen(argv[0], "rb");
    if (!file) return cmdr_error(error, "read %s: %s", argv[0], strerror(errno));
    size_t used = 0, capacity = 4096;
    char *data = malloc(capacity);
    if (!data) { fclose(file); return cmdr_error(error, "out of memory"); }
    int status = 0;
    for (;;) {
        size_t n = fread(data + used, 1, capacity - used - 1, file);
        if (memchr(data + used, '\0', n)) {
            status = cmdr_error(error, "read: binary data is not supported");
            break;
        }
        used += n;
        if (ferror(file)) { status = cmdr_error(error, "read %s failed", argv[0]); break; }
        if (feof(file)) break;
        if (capacity > (size_t)-1 / 2) {
            status = cmdr_error(error, "file is too large");
            break;
        }
        capacity *= 2;
        char *next = realloc(data, capacity);
        if (!next) { status = cmdr_error(error, "out of memory"); break; }
        data = next;
    }
    fclose(file);
    if (status) free(data);
    else { data[used] = '\0'; *result = data; }
    return status;
}

/* Every helper produces owned values, just like application commands. */
static int copy_range(const char *text, size_t n, char **result, char **error)
{
    *result = malloc(n + 1);
    if (!*result) return cmdr_error(error, "out of memory");
    memcpy(*result, text, n);
    (*result)[n] = '\0';
    return 0;
}

static int size_result(size_t value, char **result, char **error)
{
    char text[3 * sizeof(size_t) + 1];
    snprintf(text, sizeof(text), "%zu", value);
    return copy_range(text, strlen(text), result, error);
}

static int lower_command(void *user, size_t argc, const char *const *argv,
                         char **result, char **error)
{
    (void)user;
    if (argc != 1) return cmdr_error(error, "usage: lower TEXT");
    if (copy_range(argv[0], strlen(argv[0]), result, error)) return 1;
    for (char *p = *result; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return 0;
}

static int trim_command(void *user, size_t argc, const char *const *argv,
                        char **result, char **error)
{
    (void)user;
    if (argc != 1) return cmdr_error(error, "usage: trim TEXT");
    const char *start = argv[0], *end = start + strlen(start);
    while (start < end && isspace((unsigned char)*start)) ++start;
    while (end > start && isspace((unsigned char)end[-1])) --end;
    return copy_range(start, (size_t)(end - start), result, error);
}

static int length_command(void *user, size_t argc, const char *const *argv,
                          char **result, char **error)
{
    (void)user;
    if (argc != 1) return cmdr_error(error, "usage: length TEXT");
    return size_result(strlen(argv[0]), result, error);
}

typedef struct {
    char *data;
    size_t length, capacity;
} buffer;

static int append(buffer *b, const char *text, size_t n, char **error)
{
    if (n > SIZE_MAX - b->length - 1) return cmdr_error(error, "value is too large");
    size_t needed = b->length + n + 1;
    if (needed > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 64;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        char *next = realloc(b->data, capacity);
        if (!next) return cmdr_error(error, "out of memory");
        b->data = next;
        b->capacity = capacity;
    }
    memcpy(b->data + b->length, text, n);
    b->length += n;
    b->data[b->length] = '\0';
    return 0;
}

static int finish(buffer *b, char **result, char **error)
{
    if (!b->data && append(b, "", 0, error)) return 1;
    *result = b->data;
    return 0;
}

static int replace_command(void *user, size_t argc, const char *const *argv,
                           char **result, char **error)
{
    (void)user;
    if (argc != 3 || !argv[1][0])
        return cmdr_error(error, "usage: replace TEXT NONEMPTY_SEARCH REPLACEMENT");
    const char *p = argv[0], *match;
    size_t search = strlen(argv[1]), replacement = strlen(argv[2]);
    buffer b = {0};
    while ((match = strstr(p, argv[1]))) {
        if (append(&b, p, (size_t)(match - p), error) ||
            append(&b, argv[2], replacement, error)) goto fail;
        p = match + search;
    }
    if (append(&b, p, strlen(p), error)) goto fail;
    return finish(&b, result, error);
fail:
    free(b.data);
    return 1;
}

static int number(const char *text, double *value, char **error)
{
    char *end;
    errno = 0;
    *value = strtod(text, &end);
    if (end == text || *end || errno == ERANGE || !isfinite(*value))
        return cmdr_error(error, "invalid finite number: %s", text);
    return 0;
}

static int arithmetic(char op, size_t argc, const char *const *argv,
                      char **result, char **error)
{
    if (argc < 2 || ((op == '-' || op == '/') && argc != 2))
        return cmdr_error(error, "arithmetic expects %s numbers",
                          op == '-' || op == '/' ? "exactly two" : "at least two");
    double value;
    if (number(argv[0], &value, error)) return 1;
    for (size_t i = 1; i < argc; ++i) {
        double operand;
        if (number(argv[i], &operand, error)) return 1;
        if (op == '+') value += operand;
        else if (op == '-') value -= operand;
        else if (op == '*') value *= operand;
        else {
            if (operand == 0) return cmdr_error(error, "division by zero");
            value /= operand;
        }
        if (!isfinite(value)) return cmdr_error(error, "arithmetic overflow");
    }
    char text[64];
    snprintf(text, sizeof(text), "%.17g", value);
    return copy_range(text, strlen(text), result, error);
}

#define NUMBER_COMMAND(name, operation) \
    static int name##_command(void *user, size_t argc, const char *const *argv, \
                               char **result, char **error) \
    { (void)user; return arithmetic(operation, argc, argv, result, error); }
NUMBER_COMMAND(add, '+')
NUMBER_COMMAND(sub, '-')
NUMBER_COMMAND(mul, '*')
NUMBER_COMMAND(div, '/')
#undef NUMBER_COMMAND

static int print_command(void *user, size_t argc, const char *const *argv,
                         char **result, char **error)
{
    (void)user;
    if (argc != 1) return cmdr_error(error, "usage: print TEXT");
    if (copy_range(argv[0], strlen(argv[0]), result, error)) return 1;
    if (puts(argv[0]) == EOF || fflush(stdout) == EOF)
        return cmdr_error(error, "print: output failed");
    return 0;
}

/* Keep temporary streams above the standard descriptors, even in an embedding
 * application that has closed stdin/stdout. Originals must not survive exec. */
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

/* File-backed capture avoids pipe deadlocks when a child writes output before
 * reading its input. Pipelines already evaluate one complete value at a time. */
static int execute(const char *input, size_t argc, const char *const *argv,
                   char **result, char **error)
{
    if (!argc || !argv[0][0]) return cmdr_error(error, "expected an executable name");
    char **args = calloc(argc + 1, sizeof(*args));
    if (!args) return cmdr_error(error, "out of memory");
    for (size_t i = 0; i < argc; ++i) args[i] = (char *)argv[i];
    FILE *in = process_stream();
    FILE *out = in ? process_stream() : NULL;
    int status = 1;
    buffer b = {0};
    if (!in || !out) {
        cmdr_error(error, "run: temporary stream: %s", strerror(errno));
        goto done;
    }
    size_t n = strlen(input);
    if (fwrite(input, 1, n, in) != n || fflush(in) || fseek(in, 0, SEEK_SET)) {
        cmdr_error(error, "run: preparing stdin: %s", strerror(errno));
        goto done;
    }
    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc) { cmdr_error(error, "run: %s", strerror(rc)); goto done; }
    rc = posix_spawn_file_actions_adddup2(&actions, fileno(in), STDIN_FILENO);
    if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, fileno(out), STDOUT_FILENO);
    if (!rc) rc = posix_spawn_file_actions_addclose(&actions, fileno(in));
    if (!rc) rc = posix_spawn_file_actions_addclose(&actions, fileno(out));
    pid_t child = 0;
    if (!rc) rc = posix_spawnp(&child, args[0], &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc) { cmdr_error(error, "run %s: %s", argv[0], strerror(rc)); goto done; }
    int wait_status;
    pid_t waited;
    do { waited = waitpid(child, &wait_status, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        cmdr_error(error, "run %s: wait: %s", argv[0], strerror(errno));
        goto done;
    }
    if (WIFSIGNALED(wait_status)) {
        cmdr_error(error, "run %s: terminated by signal %d", argv[0], WTERMSIG(wait_status));
        goto done;
    }
    if (!WIFEXITED(wait_status) || WEXITSTATUS(wait_status)) {
        cmdr_error(error, "run %s: exited with status %d", argv[0],
                   WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1);
        goto done;
    }
    if (fseek(out, 0, SEEK_SET)) {
        cmdr_error(error, "run: seeking stdout: %s", strerror(errno));
        goto done;
    }
    char chunk[4096];
    while ((n = fread(chunk, 1, sizeof(chunk), out))) {
        if (memchr(chunk, '\0', n)) {
            cmdr_error(error, "run %s: binary stdout is not supported", argv[0]);
            goto done;
        }
        if (append(&b, chunk, n, error)) goto done;
    }
    if (ferror(out)) { cmdr_error(error, "run: reading stdout failed"); goto done; }
    status = finish(&b, result, error);
    if (!status) b.data = NULL;
done:
    free(b.data);
    if (in) fclose(in);
    if (out) fclose(out);
    free(args);
    return status;
}

static int run_command(void *user, size_t argc, const char *const *argv,
                       char **result, char **error)
{
    (void)user;
    if (!argc) return cmdr_error(error, "usage: run PROGRAM [ARG ...]");
    return execute("", argc, argv, result, error);
}

static int run_in_command(void *user, size_t argc, const char *const *argv,
                          char **result, char **error)
{
    (void)user;
    if (argc < 2) return cmdr_error(error, "usage: run-in TEXT PROGRAM [ARG ...]");
    return execute(argv[0], argc - 1, argv + 1, result, error);
}

const cmdr *cmdr_prelude(void)
{
    static const cmdr_command commands[] = {
        {.name = "text", .usage = "text [ARG ...]", .docstring = "Join arguments with spaces", .run = text_command},
        {.name = "upper", .usage = "upper TEXT", .docstring = "Uppercase text", .run = upper_command},
        {.name = "lower", .usage = "lower TEXT", .docstring = "Lowercase text", .run = lower_command},
        {.name = "trim", .usage = "trim TEXT", .docstring = "Trim surrounding whitespace", .run = trim_command},
        {.name = "length", .usage = "length TEXT", .docstring = "Count bytes", .run = length_command},
        {.name = "replace", .usage = "replace TEXT OLD NEW", .docstring = "Replace all literal matches (OLD nonempty)", .run = replace_command},
        {.name = "add", .usage = "add A B [NUMBER ...]", .docstring = "Add numbers", .run = add_command},
        {.name = "sub", .usage = "sub A B", .docstring = "Subtract B from A", .run = sub_command},
        {.name = "mul", .usage = "mul A B [NUMBER ...]", .docstring = "Multiply numbers", .run = mul_command},
        {.name = "div", .usage = "div A B", .docstring = "Divide A by B", .run = div_command},
        {.name = "read", .usage = "read PATH", .docstring = "Read a text file", .run = read_command},
        {.name = "print", .usage = "print TEXT", .docstring = "Print TEXT plus newline, returning TEXT", .run = print_command},
        {.name = "run", .usage = "run PROGRAM [ARG ...]", .docstring = "Run an executable, returning stdout", .run = run_command},
        {.name = "run-in", .usage = "run-in TEXT PROGRAM [ARG ...]", .docstring = "Run with TEXT on stdin, returning stdout", .run = run_in_command},
    };
    static const cmdr prelude = {
        .commands = commands,
        .count = sizeof(commands) / sizeof(commands[0]),
        .parent = &cmdr_essentials,
    };
    return &prelude;
}
