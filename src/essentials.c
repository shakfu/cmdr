#define _POSIX_C_SOURCE 200809L
#include "essentials.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int require_text(const cmdr_value *v, char **error)
{
    return v->kind == CMDR_TEXT ? 0 : cmdr_error(error, "expected text");
}

static int text_range(const char *p, size_t n, cmdr_value *out, char **error)
{
    char *copy = malloc(n + 1);
    if (!copy) return cmdr_error(error, "out of memory");
    memcpy(copy, p, n);
    copy[n] = '\0';
    *out = (cmdr_value){.kind = CMDR_TEXT, .text = copy};
    return 0;
}

static int push_text(cmdr_value *list, const char *p, size_t n, char **error)
{
    cmdr_value item = {0};
    if (text_range(p, n, &item, error)) return 1;
    if (cmdr_value_push(list, &item, error)) { cmdr_value_free(&item); return 1; }
    return 0;
}

static int push_copy(cmdr_value *list, const cmdr_value *item, char **error)
{
    cmdr_value copy = {0};
    if (cmdr_value_clone(item, &copy, error)) return 1;
    if (cmdr_value_push(list, &copy, error)) { cmdr_value_free(&copy); return 1; }
    return 0;
}

/* Legacy newline input remains accepted. A final newline terminates a record;
 * actual lists retain empty elements and embedded newlines without parsing. */
static int as_list(const cmdr_value *v, cmdr_value *out, char **error)
{
    if (v->kind == CMDR_LIST) return cmdr_value_clone(v, out, error);
    if (require_text(v, error)) return 1;
    *out = (cmdr_value){.kind = CMDR_LIST};
    const char *p = v->text;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (push_text(out, p, n, error)) { cmdr_value_free(out); return 1; }
        if (!end) break;
        p = end + 1;
    }
    return 0;
}

static int natural(const cmdr_value *v, size_t *value, char **error)
{
    if (require_text(v, error)) return 1;
    const char *p = v->text;
    if (!*p) return cmdr_error(error, "expected a nonnegative integer");
    size_t n = 0;
    for (; *p; ++p) {
        if (*p < '0' || *p > '9' || n > (SIZE_MAX - (size_t)(*p - '0')) / 10)
            return cmdr_error(error, "invalid nonnegative integer: %s", v->text);
        n = n * 10 + (size_t)(*p - '0');
    }
    *value = n;
    return 0;
}

#define CALLBACK(name) \
    static int name(const cmdr *repl, void *user, size_t argc, \
                    const cmdr_value *argv, cmdr_value *result, char **error)
#define UNUSED() do { (void)repl; (void)user; } while (0)

CALLBACK(list_command)
{
    UNUSED();
    result->kind = CMDR_LIST;
    for (size_t i = 0; i < argc; ++i)
        if (push_copy(result, &argv[i], error)) return 1;
    return 0;
}

CALLBACK(split_command)
{
    UNUSED();
    if (argc != 2) return cmdr_error(error, "usage: split TEXT NONEMPTY_SEPARATOR");
    if (require_text(&argv[0], error) || require_text(&argv[1], error)) return 1;
    const char *p = argv[0].text, *sep = argv[1].text, *match;
    size_t n = strlen(sep);
    if (!n) return cmdr_error(error, "split: separator must be nonempty");
    result->kind = CMDR_LIST;
    if (!*p) return 0;
    while ((match = strstr(p, sep))) {
        if (push_text(result, p, (size_t)(match - p), error)) return 1;
        p = match + n;
    }
    return push_text(result, p, strlen(p), error);
}

CALLBACK(join_command)
{
    UNUSED();
    if (argc != 2) return cmdr_error(error, "usage: join LIST_OR_LINES SEPARATOR");
    if (require_text(&argv[1], error)) return 1;
    cmdr_value list = {0};
    if (as_list(&argv[0], &list, error)) return 1;
    size_t size = 1, sep = strlen(argv[1].text);
    int status = 1;
    for (size_t i = 0; i < list.count; ++i) {
        if (require_text(&list.items[i], error)) goto done;
        size_t n = strlen(list.items[i].text), gap = i ? sep : 0;
        if (gap > SIZE_MAX - size || n > SIZE_MAX - size - gap) {
            cmdr_error(error, "joined value is too large");
            goto done;
        }
        size += n + gap;
    }
    result->text = malloc(size);
    if (!result->text) { cmdr_error(error, "out of memory"); goto done; }
    result->kind = CMDR_TEXT;
    char *p = result->text;
    for (size_t i = 0; i < list.count; ++i) {
        if (i) { memcpy(p, argv[1].text, sep); p += sep; }
        size_t n = strlen(list.items[i].text);
        memcpy(p, list.items[i].text, n);
        p += n;
    }
    *p = '\0';
    status = 0;
done:
    cmdr_value_free(&list);
    return status;
}

static int compare_values(const void *a, const void *b)
{
    return strcmp(((const cmdr_value *)a)->text, ((const cmdr_value *)b)->text);
}

enum operation { SORT, UNIQ, HEAD, TAIL, FILTER, COUNT };
static int collection(enum operation op, size_t argc, const cmdr_value *argv,
                       cmdr_value *result, char **error)
{
    const char *names[] = {"sort", "uniq", "head", "tail", "filter", "count"};
    int extra = op == HEAD || op == TAIL || op == FILTER;
    if (argc != (size_t)(extra ? 2 : 1))
        return cmdr_error(error, "usage: %s LIST_OR_LINES%s", names[op],
                          op == FILTER ? " SUBSTRING" : extra ? " N" : "");
    size_t limit = 0;
    if ((op == HEAD || op == TAIL) && natural(&argv[1], &limit, error)) return 1;
    if (op == FILTER && require_text(&argv[1], error)) return 1;
    cmdr_value list = {0};
    if (as_list(&argv[0], &list, error)) return 1;
    int status = 1;
    if (op == COUNT) {
        char text[3 * sizeof(size_t) + 1];
        snprintf(text, sizeof(text), "%zu", list.count);
        status = cmdr_value_text(result, text, error);
        goto done;
    }
    if (op == SORT || op == UNIQ || op == FILTER)
        for (size_t i = 0; i < list.count; ++i)
            if (require_text(&list.items[i], error)) goto done;
    if (op == SORT && list.count > 1)
        qsort(list.items, list.count, sizeof(*list.items), compare_values);
    result->kind = CMDR_LIST;
    for (size_t i = 0; i < list.count; ++i) {
        if (op == UNIQ && i && !strcmp(list.items[i].text, list.items[i - 1].text)) continue;
        if (op == HEAD && i >= limit) continue;
        if (op == TAIL && limit < list.count && i < list.count - limit) continue;
        if (op == FILTER && !strstr(list.items[i].text, argv[1].text)) continue;
        if (push_copy(result, &list.items[i], error)) goto done;
    }
    status = 0;
    /* Preserve text pipelines; explicit lists keep their item boundaries. */
    if (argv[0].kind == CMDR_TEXT) {
        char *text = NULL;
        status = cmdr_value_render(result, &text, error);
        cmdr_value_free(result);
        if (!status) *result = (cmdr_value){.kind = CMDR_TEXT, .text = text};
    }
done:
    cmdr_value_free(&list);
    return status;
}

#define COLLECTION(name, op) \
    CALLBACK(name##_command) \
    { UNUSED(); return collection(op, argc, argv, result, error); }
COLLECTION(sort, SORT)
COLLECTION(uniq, UNIQ)
COLLECTION(head, HEAD)
COLLECTION(tail, TAIL)
COLLECTION(filter, FILTER)
COLLECTION(count, COUNT)
#undef COLLECTION

CALLBACK(map_command)
{
    (void)user;
    if (argc < 2) return cmdr_error(error, "usage: map LIST_OR_LINES COMMAND [ARG ...]");
    if (require_text(&argv[1], error)) return 1;
    const char *name = argv[1].text;
    if (!cmdr_find(repl, name)) return cmdr_error(error, "unknown command: %s", name);
    cmdr_value list = {0};
    if (as_list(&argv[0], &list, error)) return 1;
    cmdr_value *args = calloc(argc - 1, sizeof(*args));
    if (!args) { cmdr_value_free(&list); return cmdr_error(error, "out of memory"); }
    for (size_t i = 2; i < argc; ++i) args[i - 1] = argv[i];
    result->kind = CMDR_LIST;
    int status = 0;
    for (size_t i = 0; i < list.count; ++i) {
        args[0] = list.items[i];
        cmdr_value item = {0};
        char *detail = NULL;
        status = cmdr_call(repl, name, argc - 1, args, &item, &detail);
        if (status || item.kind == CMDR_NONE) {
            status = cmdr_error(error, "map item %zu (%s): %s", i + 1, name,
                                detail ? detail : "command returned no value");
            free(detail);
            cmdr_value_free(&item);
            break;
        }
        free(detail);
        status = cmdr_value_push(result, &item, error);
        cmdr_value_free(&item);
        if (status) break;
    }
    free(args);
    cmdr_value_free(&list);
    return status;
}

enum slicing { TAKE, DROP, SLICE };
static int slice(enum slicing op, size_t argc, const cmdr_value *argv,
                  cmdr_value *result, char **error)
{
    const char *names[] = {"take", "drop", "slice"};
    if (argc != (size_t)(op == SLICE ? 3 : 2))
        return cmdr_error(error, "usage: %s TEXT %s", names[op], op == SLICE ? "START LENGTH" : "N");
    if (require_text(&argv[0], error)) return 1;
    size_t n = strlen(argv[0].text), first, length;
    if (natural(&argv[1], &first, error)) return 1;
    if (op == SLICE && natural(&argv[2], &length, error)) return 1;
    if (op == TAKE) return text_range(argv[0].text, first < n ? first : n, result, error);
    if (first > n) first = n;
    if (op == DROP || length > n - first) length = n - first;
    return text_range(argv[0].text + first, length, result, error);
}
#define SLICING(name, op) \
    CALLBACK(name##_command) \
    { UNUSED(); return slice(op, argc, argv, result, error); }
SLICING(take, TAKE)
SLICING(drop, DROP)
SLICING(slice, SLICE)
#undef SLICING

static int path_part(int directory, size_t argc, const cmdr_value *argv,
                      cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: %s PATH", directory ? "dirname" : "basename");
    if (require_text(&argv[0], error)) return 1;
    const char *path = argv[0].text;
    size_t end = strlen(path);
    if (!end) return cmdr_value_text(result, ".", error);
    while (end > 1 && path[end - 1] == '/') --end;
    if (end == 1 && path[0] == '/') return cmdr_value_text(result, "/", error);
    size_t start = end;
    while (start && path[start - 1] != '/') --start;
    if (!directory) return text_range(path + start, end - start, result, error);
    while (start > 1 && path[start - 1] == '/') --start;
    return start ? text_range(path, start, result, error) : cmdr_value_text(result, ".", error);
}
CALLBACK(basename_command)
{ UNUSED(); return path_part(0, argc, argv, result, error); }
CALLBACK(dirname_command)
{ UNUSED(); return path_part(1, argc, argv, result, error); }

CALLBACK(files_command)
{
    UNUSED();
    if (argc > 1) return cmdr_error(error, "usage: files [DIRECTORY]");
    if (argc && require_text(&argv[0], error)) return 1;
    const char *path = argc ? argv[0].text : ".";
    DIR *directory = opendir(path);
    if (!directory) return cmdr_error(error, "files %s: %s", path, strerror(errno));
    result->kind = CMDR_LIST;
    int status = 0;
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            if (errno) status = cmdr_error(error, "files %s: %s", path, strerror(errno));
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat info;
        if (fstatat(dirfd(directory), entry->d_name, &info, 0)) {
            if (errno == ENOENT) continue; /* Broken links or entries removed during listing. */
            status = cmdr_error(error, "files %s/%s: %s", path, entry->d_name, strerror(errno));
            break;
        }
        if (!S_ISREG(info.st_mode)) continue;
        size_t prefix = strlen(path), name = strlen(entry->d_name);
        int slash = prefix && path[prefix - 1] != '/';
        if (name > SIZE_MAX - 2 || prefix > SIZE_MAX - name - 2) {
            status = cmdr_error(error, "path is too long");
            break;
        }
        char *full = malloc(prefix + slash + name + 1);
        if (!full) { status = cmdr_error(error, "out of memory"); break; }
        memcpy(full, path, prefix);
        if (slash) full[prefix++] = '/';
        memcpy(full + prefix, entry->d_name, name + 1);
        cmdr_value item = {.kind = CMDR_TEXT, .text = full};
        status = cmdr_value_push(result, &item, error);
        cmdr_value_free(&item);
        if (status) break;
    }
    if (closedir(directory) && !status)
        status = cmdr_error(error, "files %s: close failed", path);
    if (!status && result->count > 1)
        qsort(result->items, result->count, sizeof(*result->items), compare_values);
    return status;
}

static int save(int append_mode, const char *name, size_t argc, const cmdr_value *argv,
                 cmdr_value *result, char **error)
{
    if (argc != 2) return cmdr_error(error, "usage: %s VALUE PATH", name);
    if (require_text(&argv[1], error)) return 1;
    char *text = NULL;
    if (cmdr_value_render(&argv[0], &text, error)) return 1;
    if (!text) return cmdr_error(error, "cannot write a missing value");
    if (cmdr_value_clone(&argv[0], result, error)) { free(text); return 1; }
    const char *path = argv[1].text;
    FILE *file = fopen(path, append_mode ? "ab" : "wb");
    if (!file) { free(text); return cmdr_error(error, "%s %s: %s", name, path, strerror(errno)); }
    size_t n = strlen(text);
    int failed = fwrite(text, 1, n, file) != n;
    if (fclose(file)) failed = 1;
    free(text);
    return failed ? cmdr_error(error, "writing %s failed", path) : 0;
}
CALLBACK(append_command)
{ UNUSED(); return save(1, "append", argc, argv, result, error); }
CALLBACK(tee_command)
{ UNUSED(); return save(0, "tee", argc, argv, result, error); }
CALLBACK(write_command)
{ UNUSED(); return save(0, "write", argc, argv, result, error); }

CALLBACK(try_command)
{
    (void)user;
    if (argc != 1 && argc != 2) return cmdr_error(error, "usage: try [INPUT] 'PIPELINE'");
    if (require_text(&argv[argc - 1], error)) return 1;
    char *detail = NULL;
    int status = cmdr_eval_value(repl, argv[argc - 1].text, argc == 2 ? &argv[0] : NULL,
                                 result, &detail);
    if (!status) { free(detail); return 0; }
    status = cmdr_value_text(result, detail ? detail : "command failed", error);
    free(detail);
    if (!status) result->kind = CMDR_FAILURE;
    return status;
}

CALLBACK(recover_command)
{
    UNUSED();
    if (argc != 2) return cmdr_error(error, "usage: recover VALUE FALLBACK");
    return cmdr_value_clone(argv[0].kind == CMDR_FAILURE ? &argv[1] : &argv[0], result, error);
}

#define ENTRY(n, u, f) {.name = n, .usage = u, .run_value = f}
static const cmdr_command commands[] = {
    ENTRY("list", "list [VALUE ...]             Construct an explicit list", list_command),
    ENTRY("split", "split TEXT SEPARATOR         Split into an explicit list", split_command),
    ENTRY("join", "join LIST_OR_LINES SEPARATOR  Join text items", join_command),
    ENTRY("sort", "sort LIST_OR_LINES           Sort text items by byte order", sort_command),
    ENTRY("uniq", "uniq LIST_OR_LINES           Remove adjacent duplicate text items", uniq_command),
    ENTRY("head", "head LIST_OR_LINES N         Keep the first N items", head_command),
    ENTRY("tail", "tail LIST_OR_LINES N         Keep the last N items", tail_command),
    ENTRY("filter", "filter LIST_OR_LINES TEXT    Keep text items containing TEXT", filter_command),
    ENTRY("count", "count LIST_OR_LINES          Count items", count_command),
    ENTRY("map", "map LIST_OR_LINES COMMAND [ARG ...]  Apply a command to each item", map_command),
    ENTRY("files", "files [DIRECTORY]            List regular files, sorted by path", files_command),
    ENTRY("basename", "basename PATH                Return the final path component", basename_command),
    ENTRY("dirname", "dirname PATH                 Return the parent path", dirname_command),
    ENTRY("take", "take TEXT N                  Take the first N bytes", take_command),
    ENTRY("drop", "drop TEXT N                  Drop the first N bytes", drop_command),
    ENTRY("slice", "slice TEXT START LENGTH      Extract bytes at a zero-based offset", slice_command),
    ENTRY("append", "append VALUE PATH            Append to a file, returning VALUE", append_command),
    ENTRY("write", "write VALUE PATH             Replace a file, returning VALUE", write_command),
    ENTRY("tee", "tee VALUE PATH               Replace a file, returning VALUE", tee_command),
    ENTRY("try", "try [INPUT] 'PIPELINE'        Capture a pipeline failure", try_command),
    {.name = "recover", .usage = "recover VALUE FALLBACK       Substitute FALLBACK after try fails",
     .run_value = recover_command, .handles_failure = 1},
};
#undef ENTRY
#undef CALLBACK
#undef UNUSED

const cmdr cmdr_essentials = {
    .commands = commands,
    .count = sizeof(commands) / sizeof(commands[0]),
};
