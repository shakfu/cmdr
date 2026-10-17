#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define COMMAND(name) \
    static int name(const cmdr *repl, size_t argc, const cmdr_value *argv, \
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

static int natural(const cmdr_value *v, size_t *value, char **error)
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

static int number(const char *text, double *value, char **error)
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
    if (number(argv[0].text, &value, error)) return 1;
    for (size_t i = 1; i < argc; ++i) {
        if (number(argv[i].text, &operand, error)) return 1;
        if (op == '/' && operand == 0) return cmdr_error(error, "division by zero");
        value = op == '+' ? value + operand : op == '-' ? value - operand
              : op == '*' ? value * operand : value / operand;
        if (!isfinite(value)) return cmdr_error(error, "arithmetic overflow");
    }
    char text[64];
    snprintf(text, sizeof(text), "%.17g", value);
    *result = cmdr_text(text, strlen(text));
    return 0;
}
COMMAND(add_command) { return arithmetic('+', argc, argv, result, error); }
COMMAND(sub_command) { return arithmetic('-', argc, argv, result, error); }
COMMAND(mul_command) { return arithmetic('*', argc, argv, result, error); }
COMMAND(div_command) { return arithmetic('/', argc, argv, result, error); }

static int compare(int greater, size_t argc, const cmdr_value *argv,
                   cmdr_value *result, char **error)
{
    const char *name = greater ? "gt" : "lt";
    if (argc != 2) return cmdr_error(error, "usage: %s NUMBER LIMIT", name);
    double a, b;
    if (number(argv[0].text, &a, error) || number(argv[1].text, &b, error)) return 1;
    if (greater ? !(a > b) : !(a < b))
        return cmdr_error(error, "%s: %s is not %s %s", name, argv[0].text,
                          greater ? "greater than" : "less than", argv[1].text), CMDR_MISMATCH;
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return 0;
}
COMMAND(lt_command) { return compare(0, argc, argv, result, error); }
COMMAND(gt_command) { return compare(1, argc, argv, result, error); }

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

/* Files rather than pipes: a child that writes before reading cannot block. */
static int execute(const char *input, size_t argc, const cmdr_value *argv,
                   cmdr_value *result, char **error)
{
    const char *program = argv[0].text;
    if (!program[0]) return cmdr_error(error, "expected an executable name");
    char **args = cmdr_realloc(NULL, argc + 1, sizeof(*args));
    for (size_t i = 0; i < argc; ++i) args[i] = argv[i].text;
    args[argc] = NULL;
    FILE *in = process_stream(), *out = in ? process_stream() : NULL;
    int status = 1;
    size_t n = strlen(input);
    if (!in || !out) {
        cmdr_error(error, "run: temporary file: %s", strerror(errno));
        goto done;
    }
    if (fwrite(input, 1, n, in) != n || fflush(in) || fseek(in, 0, SEEK_SET)) {
        cmdr_error(error, "run: preparing stdin: %s", strerror(errno));
        goto done;
    }
    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc) { cmdr_error(error, "run: %s", strerror(rc)); goto done; }
    rc = posix_spawn_file_actions_adddup2(&actions, fileno(in), STDIN_FILENO);
    if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, fileno(out), STDOUT_FILENO);
    pid_t child;
    if (!rc) rc = posix_spawnp(&child, program, &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc) { cmdr_error(error, "run %s: %s", program, strerror(rc)); goto done; }
    int wait_status;
    pid_t waited;
    do waited = waitpid(child, &wait_status, 0); while (waited < 0 && errno == EINTR);
    if (waited < 0)
        cmdr_error(error, "run %s: wait: %s", program, strerror(errno));
    else if (WIFSIGNALED(wait_status))
        cmdr_error(error, "run %s: terminated by signal %d", program, WTERMSIG(wait_status));
    else if (WEXITSTATUS(wait_status))
        cmdr_error(error, "run %s: exited with status %d", program, WEXITSTATUS(wait_status));
    else if (fseek(out, 0, SEEK_SET))
        cmdr_error(error, "run: reading stdout: %s", strerror(errno));
    else
        status = slurp(out, program, result, error);
done:
    if (in) fclose(in);
    if (out) fclose(out);
    free(args);
    return status;
}

COMMAND(run_command)
{
    if (!argc) return cmdr_error(error, "usage: run PROGRAM [ARG ...]");
    return execute("", argc, argv, result, error);
}

COMMAND(run_in_command)
{
    if (argc < 2) return cmdr_error(error, "usage: run-in TEXT PROGRAM [ARG ...]");
    return execute(argv[0].text, argc - 1, argv + 1, result, error);
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

COMMAND(equals_command)
{
    if (argc != 2) return cmdr_error(error, "usage: equals TEXT EXPECTED");
    if (strcmp(argv[0].text, argv[1].text))
        return cmdr_error(error, "equals: text differs from '%s'", argv[1].text), CMDR_MISMATCH;
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return 0;
}

COMMAND(contains_command)
{
    if (argc != 2) return cmdr_error(error, "usage: contains TEXT SUBSTRING");
    if (!strstr(argv[0].text, argv[1].text))
        return cmdr_error(error, "contains: '%s' not found", argv[1].text), CMDR_MISMATCH;
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
    if (natural(&argv[1], &count, error)) return 1;
    if (count > n) count = n;
    *result = drop ? cmdr_text(argv[0].text + count, n - count) : cmdr_text(argv[0].text, count);
    return 0;
}
COMMAND(take_command) { return bytes(0, argc, argv, result, error); }
COMMAND(drop_command) { return bytes(1, argc, argv, result, error); }

static int path_part(int directory, size_t argc, const cmdr_value *argv,
                     cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: %s PATH", directory ? "dirname" : "basename");
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

enum operation { SORT, UNIQ, HEAD, TAIL, FILTER, COUNT };
static int collection(enum operation op, size_t argc, const cmdr_value *argv,
                      cmdr_value *result, char **error)
{
    static const char *names[] = {"sort", "uniq", "head", "tail", "filter", "count"};
    int extra = op == HEAD || op == TAIL || op == FILTER;
    if (argc != (size_t)(extra ? 2 : 1))
        return cmdr_error(error, "usage: %s LIST_OR_LINES%s", names[op],
                          op == FILTER ? " SUBSTRING" : extra ? " N" : "");
    size_t limit = 0;
    if ((op == HEAD || op == TAIL) && natural(&argv[1], &limit, error)) return 1;
    if (op == FILTER && require_text(&argv[1], error)) return 1;
    cmdr_value list;
    if (as_list(&argv[0], &list, error)) return 1;
    if (op == COUNT) {
        size_result(list.count, result);
        cmdr_free(&list);
        return 0;
    }
    for (size_t i = 0; (op == SORT || op == UNIQ || op == FILTER) && i < list.count; ++i)
        if (require_text(&list.items[i], error)) { cmdr_free(&list); return 1; }
    if (op == SORT) qsort(list.items, list.count, sizeof(*list.items), by_text);
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
static int run_body(const cmdr *repl, const cmdr_value *subject, size_t argc,
                    const cmdr_value *argv, cmdr_value *out, char **detail)
{
    cmdr_value *args = cmdr_realloc(NULL, argc - 1, sizeof(*args));
    args[0] = *subject;
    for (size_t i = 2; i < argc; ++i) args[i - 1] = argv[i];
    int status = cmdr_run(repl, &argv[1], argc - 1, args, out, detail);
    free(args);
    return status;
}

static int per_item(int keep, const cmdr *repl, size_t argc, const cmdr_value *argv,
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
        int failed = run_body(repl, &list.items[i], argc, argv, &item, &detail);
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
COMMAND(each_command) { return per_item(0, repl, argc, argv, result, error); }
COMMAND(keep_command) { return per_item(1, repl, argc, argv, result, error); }

COMMAND(not_command)
{
    if (argc < 2) return cmdr_error(error, "usage: not VALUE BLOCK [ARG ...]");
    if (check_body("not", &argv[1], argc - 1, error)) return 1;
    cmdr_value out;
    char *detail;
    int failed = run_body(repl, &argv[0], argc, argv, &out, &detail);
    cmdr_free(&out);
    int status = failed;
    if (failed == CMDR_MISMATCH) {
        status = cmdr_clone(&argv[0], result, error);
    } else if (failed) {
        cmdr_error(error, "not: %s", detail);
    } else {
        cmdr_error(error, "not: {%s} succeeded", argv[1].text);
        status = CMDR_MISMATCH;
    }
    free(detail);
    return status;
}

COMMAND(try_command)
{
    if (argc != 2 && argc != 3) return cmdr_error(error, "usage: try [INPUT] BLOCK FALLBACK");
    const cmdr_value *body = &argv[argc - 2];
    if (check_body("try", body, argc - 2, error)) return 1;
    char *detail;
    int failed = cmdr_run(repl, body, argc - 2, argv, result, &detail);
    free(detail);
    return failed ? cmdr_clone(&argv[argc - 1], result, error) : 0;
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
static const cmdr_command commands[] = {
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
    TEXT("equals", "equals TEXT EXPECTED", "Return TEXT if equal to EXPECTED", equals_command),
    TEXT("contains", "contains TEXT SUBSTRING", "Return TEXT if it contains SUBSTRING", contains_command),
    VALUE("nonempty", "nonempty VALUE", "Return VALUE unless empty text or list", nonempty_command),
    VALUE("list", "list [VALUE ...]", "Construct a list", list_command),
    VALUE("lines", "lines LIST_OR_LINES", "Split text into a list of lines", lines_command),
    VALUE("join", "join LIST_OR_LINES SEPARATOR", "Join text items", join_command),
    VALUE("sort", "sort LIST_OR_LINES", "Sort text items by byte order", sort_command),
    VALUE("uniq", "uniq LIST_OR_LINES", "Remove adjacent duplicate text items", uniq_command),
    VALUE("head", "head LIST_OR_LINES N", "Keep the first N items", head_command),
    VALUE("tail", "tail LIST_OR_LINES N", "Keep the last N items", tail_command),
    VALUE("filter", "filter LIST_OR_LINES TEXT", "Keep text items containing TEXT", filter_command),
    VALUE("count", "count LIST_OR_LINES", "Count items", count_command),
    VALUE("map", "map LIST_OR_LINES COMMAND [ARG ...]", "Apply a command to each item", map_command),
    VALUE("each", "each LIST_OR_LINES BLOCK [ARG ...]", "Call a block per item", each_command),
    VALUE("keep", "keep LIST_OR_LINES BLOCK [ARG ...]", "Keep items whose block succeeds", keep_command),
    VALUE("not", "not VALUE BLOCK [ARG ...]", "Return VALUE if the block mismatches", not_command),
    VALUE("try", "try [INPUT] BLOCK FALLBACK", "Return FALLBACK if the block fails", try_command),
    TEXT("read", "read PATH", "Read a text file", read_command),
    VALUE("write", "write VALUE PATH", "Replace a file, returning VALUE", write_command),
    VALUE("append", "append VALUE PATH", "Append to a file, returning VALUE", append_command),
    TEXT("files", "files [DIRECTORY]", "List regular files, sorted by path", files_command),
    TEXT("basename", "basename PATH", "Return the final path component", basename_command),
    TEXT("dirname", "dirname PATH", "Return the parent path", dirname_command),
    TEXT("run", "run PROGRAM [ARG ...]", "Run an executable, returning stdout", run_command),
    TEXT("run-in", "run-in TEXT PROGRAM [ARG ...]", "Run with TEXT on stdin, returning stdout", run_in_command),
};

const cmdr cmdr_primitives = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
