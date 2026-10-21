#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

static int same(const cmdr_value *path, cmdr_value *result)
{
    *result = cmdr_text(path->text, strlen(path->text));
    return 0;
}

static int mismatch(char **error, const char *format, const cmdr_value *path)
{
    char *text = cmdr_describe(path);
    cmdr_error(error, format, text);
    free(text);
    return CMDR_MISMATCH;
}

/* stat PATH, following links. A missing PATH is a mismatch; other failures,
 * such as a denied search, are errors. */
static int look(const char *name, const cmdr_value *path, struct stat *info, char **error)
{
    if (!stat(path->text, info)) return 0;
    if (errno == ENOENT || errno == ENOTDIR) {
        char format[64];
        snprintf(format, sizeof(format), "%s: %%s does not exist", name);
        return mismatch(error, format, path);
    }
    return cmdr_error(error, "%s %s: %s", name, path->text, strerror(errno));
}

enum kind { ANY, FILE_KIND, DIR_KIND };

static int test(enum kind kind, const char *name, size_t argc, const cmdr_value *argv,
                cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: %s PATH", name);
    struct stat info;
    int status = look(name, &argv[0], &info, error);
    if (status) return status;
    if (kind == FILE_KIND && !S_ISREG(info.st_mode))
        return mismatch(error, "fs.is-file: %s is not a regular file", &argv[0]);
    if (kind == DIR_KIND && !S_ISDIR(info.st_mode))
        return mismatch(error, "fs.is-dir: %s is not a directory", &argv[0]);
    return same(&argv[0], result);
}
COMMAND(exists_command) { return test(ANY, "fs.exists", argc, argv, result, error); }
COMMAND(is_file_command) { return test(FILE_KIND, "fs.is-file", argc, argv, result, error); }
COMMAND(is_dir_command) { return test(DIR_KIND, "fs.is-dir", argc, argv, result, error); }

static int integer(long long n, cmdr_value *result)
{
    char text[32];
    snprintf(text, sizeof(text), "%lld", n);
    *result = cmdr_text(text, strlen(text));
    return 0;
}

static int stat_number(int mtime, const char *name, size_t argc, const cmdr_value *argv,
                       cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: %s PATH", name);
    struct stat info;
    if (stat(argv[0].text, &info)) return cmdr_error(error, "%s %s: %s", name, argv[0].text, strerror(errno));
    return integer(mtime ? (long long)info.st_mtime : (long long)info.st_size, result);
}
COMMAND(size_command) { return stat_number(0, "fs.size", argc, argv, result, error); }
COMMAND(mtime_command) { return stat_number(1, "fs.mtime", argc, argv, result, error); }

static int by_text(const void *a, const void *b)
{
    return strcmp(((const cmdr_value *)a)->text, ((const cmdr_value *)b)->text);
}

/* As files does, but directories other than . and .., not following links,
 * as find -type d does. A link to an ancestor would make a walk loop. */
COMMAND(dirs_command)
{
    if (argc > 1) return cmdr_error(error, "usage: fs.dirs [DIRECTORY]");
    const char *path = argc ? argv[0].text : ".";
    DIR *directory = opendir(path);
    if (!directory) return cmdr_error(error, "fs.dirs %s: %s", path, strerror(errno));
    size_t n = strlen(path);
    const char *slash = n && path[n - 1] != '/' ? "/" : "";
    cmdr_value list = {.kind = CMDR_LIST};
    int status = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno) status = cmdr_error(error, "fs.dirs %s: %s", path, strerror(errno));
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat info;
        if (fstatat(dirfd(directory), entry->d_name, &info, AT_SYMLINK_NOFOLLOW)) {
            if (errno == ENOENT) continue; /* Removed while listing. */
            status = cmdr_error(error, "fs.dirs %s/%s: %s", path, entry->d_name, strerror(errno));
            break;
        }
        if (!S_ISDIR(info.st_mode)) continue;
        char *joined = cmdr_realloc(NULL, n + strlen(slash) + strlen(entry->d_name) + 1, 1);
        sprintf(joined, "%s%s%s", path, slash, entry->d_name);
        cmdr_push(&list, (cmdr_value){.kind = CMDR_TEXT, .text = joined});
    }
    closedir(directory);
    if (status) { cmdr_free(&list); return status; }
    if (list.count) qsort(list.items, list.count, sizeof(*list.items), by_text);
    *result = list;
    return 0;
}

/* Create PATH and missing parents, as mkdir -p does. */
COMMAND(mkdir_command)
{
    if (argc != 1) return cmdr_error(error, "usage: fs.mkdir PATH");
    const char *path = argv[0].text;
    if (!*path) return cmdr_error(error, "fs.mkdir: empty path");
    char *prefix = cmdr_strndup(path, strlen(path));
    int status = 0;
    for (char *p = prefix + 1;; ++p) {
        if (*p && *p != '/') continue;
        char end = *p;
        *p = '\0';
        struct stat info;
        if (mkdir(prefix, 0777) && (errno != EEXIST || stat(prefix, &info) || !S_ISDIR(info.st_mode))) {
            if (errno == EEXIST) errno = ENOTDIR;
            status = cmdr_error(error, "fs.mkdir %s: %s", prefix, strerror(errno));
        }
        *p = end;
        if (status || !end) break;
    }
    free(prefix);
    return status ? status : same(&argv[0], result);
}

/* A file, link, or empty directory; not recursive. */
COMMAND(remove_command)
{
    if (argc != 1) return cmdr_error(error, "usage: fs.remove PATH");
    if (remove(argv[0].text)) return cmdr_error(error, "fs.remove %s: %s", argv[0].text, strerror(errno));
    return same(&argv[0], result);
}

/* rename(2): replaces an existing file at DESTINATION. */
COMMAND(move_command)
{
    if (argc != 2) return cmdr_error(error, "usage: fs.move SOURCE DESTINATION");
    if (rename(argv[0].text, argv[1].text))
        return cmdr_error(error, "fs.move %s %s: %s", argv[0].text, argv[1].text, strerror(errno));
    return same(&argv[1], result);
}

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    TEXT("exists", "exists PATH", "Return PATH if it exists", exists_command),
    TEXT("is-file", "is-file PATH", "Return PATH if it is a regular file", is_file_command),
    TEXT("is-dir", "is-dir PATH", "Return PATH if it is a directory", is_dir_command),
    TEXT("dirs", "dirs [DIRECTORY]", "List subdirectories, sorted by path", dirs_command),
    TEXT("size", "size PATH", "Return the size in bytes", size_command),
    TEXT("mtime", "mtime PATH", "Return the modification time, in epoch seconds", mtime_command),
    TEXT("mkdir", "mkdir PATH", "Create a directory and its parents", mkdir_command),
    TEXT("remove", "remove PATH", "Delete a file or empty directory", remove_command),
    TEXT("move", "move SOURCE DESTINATION", "Rename, replacing DESTINATION", move_command),
};
const cmdr cmdr_ext_fs = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
