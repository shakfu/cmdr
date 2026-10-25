#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700 /* realpath is XSI. */
#define _DARWIN_C_SOURCE /* macOS declares mkdtemp only outside strict POSIX. */
#include "cmdr.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

/* stat PATH, following links unless asked not to. A missing PATH is a
 * mismatch; other failures, such as a denied search, are errors. */
static int look_at(int follow, const char *name, const cmdr_value *path, struct stat *info,
                   char **error)
{
    if (!(follow ? stat : lstat)(path->text, info)) return 0;
    if (errno == ENOENT || errno == ENOTDIR) {
        char format[64];
        snprintf(format, sizeof(format), "%s: %%s does not exist", name);
        return mismatch(error, format, path);
    }
    return cmdr_error(error, "%s %s: %s", name, path->text, strerror(errno));
}

static int look(const char *name, const cmdr_value *path, struct stat *info, char **error)
{
    return look_at(1, name, path, info, error);
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

COMMAND(is_link_command)
{
    if (argc != 1) return cmdr_error(error, "usage: fs.is-link PATH");
    struct stat info;
    int status = look_at(0, "fs.is-link", &argv[0], &info, error);
    if (status) return status;
    if (!S_ISLNK(info.st_mode)) return mismatch(error, "fs.is-link: %s is not a symbolic link", &argv[0]);
    return same(&argv[0], result);
}

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

/* DIRECTORY and NAME with one / between them. */
static char *join(const char *directory, const char *name)
{
    size_t n = strlen(directory);
    const char *slash = n && directory[n - 1] != '/' ? "/" : "";
    char *joined = cmdr_realloc(NULL, n + strlen(slash) + strlen(name) + 1, 1);
    sprintf(joined, "%s%s%s", directory, slash, name);
    return joined;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void free_names(char **names, size_t n)
{
    for (size_t i = 0; i < n; ++i) free(names[i]);
    free(names);
}

/* The entries of directory but . and .., sorted, then close it. Read whole
 * before any is changed: readdir may skip entries after a removal. */
static int read_names(DIR *directory, char ***names, size_t *count)
{
    char **list = NULL;
    size_t n = 0;
    int failed = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            failed = errno;
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        list = cmdr_realloc(list, n + 1, sizeof(*list));
        list[n++] = cmdr_strndup(entry->d_name, strlen(entry->d_name));
    }
    closedir(directory);
    if (failed) {
        free_names(list, n);
        errno = failed;
        return 1;
    }
    if (n) qsort(list, n, sizeof(*list), by_name);
    *names = list;
    *count = n;
    return 0;
}

/* Open NAME in the directory at at as a directory, not following a link. */
static DIR *open_dir_at(int at, const char *name)
{
    int fd = openat(at, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR *directory = fd < 0 ? NULL : fdopendir(fd);
    if (fd >= 0 && !directory) close(fd);
    return directory;
}

/* As files does, but directories other than . and .., not following links,
 * as find -type d does. A link to an ancestor would make a walk loop. */
COMMAND(dirs_command)
{
    if (argc > 1) return cmdr_error(error, "usage: fs.dirs [DIRECTORY]");
    const char *path = argc ? argv[0].text : ".";
    DIR *directory = opendir(path);
    if (!directory) return cmdr_error(error, "fs.dirs %s: %s", path, strerror(errno));
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
        cmdr_push(&list, (cmdr_value){.kind = CMDR_TEXT, .text = join(path, entry->d_name)});
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

/* Every path below path, into list. Links are listed, not
 * followed; entries removed during the walk are skipped. */
static int walk(DIR *directory, const char *path, cmdr_value *list, char **error)
{
    int at = dirfd(directory);
    char **names;
    size_t n;
    /* read_names closes directory, so keep it open for the fstatat calls. */
    int fd = dup(at);
    if (fd < 0 || read_names(directory, &names, &n)) {
        if (fd >= 0) close(fd);
        return cmdr_error(error, "fs.walk %s: %s", path, strerror(errno));
    }
    int status = 0;
    for (size_t i = 0; i < n && !status; ++i) {
        struct stat info;
        if (fstatat(fd, names[i], &info, AT_SYMLINK_NOFOLLOW)) {
            if (errno == ENOENT) continue;
            status = cmdr_error(error, "fs.walk %s/%s: %s", path, names[i], strerror(errno));
            break;
        }
        char *child = join(path, names[i]);
        cmdr_push(list, (cmdr_value){.kind = CMDR_TEXT, .text = child});
        if (!S_ISDIR(info.st_mode)) continue;
        DIR *sub = open_dir_at(fd, names[i]);
        if (sub) status = walk(sub, child, list, error);
        else if (errno != ENOENT && errno != ENOTDIR && errno != ELOOP)
            status = cmdr_error(error, "fs.walk %s: %s", child, strerror(errno));
    }
    close(fd);
    free_names(names, n);
    return status;
}

COMMAND(walk_command)
{
    if (argc > 1) return cmdr_error(error, "usage: fs.walk [DIRECTORY]");
    const char *path = argc ? argv[0].text : ".";
    DIR *directory = opendir(path);
    if (!directory) return cmdr_error(error, "fs.walk %s: %s", path, strerror(errno));
    cmdr_value list = {.kind = CMDR_LIST};
    if (walk(directory, path, &list, error)) { cmdr_free(&list); return 1; }
    if (list.count) qsort(list.items, list.count, sizeof(*list.items), by_text);
    *result = list;
    return 0;
}

/* As ln -s TARGET PATH: TARGET is stored as given, relative to PATH's directory. */
COMMAND(symlink_command)
{
    if (argc != 2) return cmdr_error(error, "usage: fs.symlink TARGET PATH");
    if (symlink(argv[0].text, argv[1].text))
        return cmdr_error(error, "fs.symlink %s %s: %s", argv[0].text, argv[1].text, strerror(errno));
    return same(&argv[1], result);
}

COMMAND(realpath_command)
{
    if (argc != 1) return cmdr_error(error, "usage: fs.realpath PATH");
    char *real = realpath(argv[0].text, NULL);
    if (!real) return cmdr_error(error, "fs.realpath %s: %s", argv[0].text, strerror(errno));
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = real};
    return 0;
}

/* TEMPLATE, which ends in XXXXXX, or cmdr-XXXXXX in TMPDIR or /tmp. */
static char *template(const char *name, size_t argc, const cmdr_value *argv, char **error)
{
    if (argc > 1) return cmdr_error(error, "usage: %s [TEMPLATE]", name), NULL;
    if (argc) {
        size_t n = strlen(argv[0].text);
        if (n < 6 || strcmp(argv[0].text + n - 6, "XXXXXX"))
            return cmdr_error(error, "%s: TEMPLATE must end in XXXXXX: %s", name, argv[0].text), NULL;
        return cmdr_strndup(argv[0].text, n);
    }
    const char *directory = getenv("TMPDIR");
    return join(directory && *directory ? directory : "/tmp", "cmdr-XXXXXX");
}

COMMAND(temp_file_command)
{
    char *path = template("fs.temp-file", argc, argv, error);
    if (!path) return 1;
    int fd = mkstemp(path);
    if (fd < 0) {
        cmdr_error(error, "fs.temp-file %s: %s", path, strerror(errno));
        free(path);
        return 1;
    }
    close(fd);
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = path};
    return 0;
}

COMMAND(temp_dir_command)
{
    char *path = template("fs.temp-dir", argc, argv, error);
    if (!path) return 1;
    if (!mkdtemp(path)) {
        cmdr_error(error, "fs.temp-dir %s: %s", path, strerror(errno));
        free(path);
        return 1;
    }
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = path};
    return 0;
}

/* PATH without its last component, as path-dirname gives it. */
static char *parent_of(const char *path)
{
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') --n;
    while (n && path[n - 1] != '/') --n;
    while (n > 1 && path[n - 1] == '/') --n;
    return n ? cmdr_strndup(path, n) : cmdr_strndup(".", 1);
}

/* 0, or the path that failed, from or to. */
static const char *copy_file(const char *from, const char *to, mode_t mode)
{
    int in = open(from, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0) return from;
    int out = open(to, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode & 0777);
    if (out < 0) {
        int saved = errno;
        close(in);
        errno = saved;
        return to;
    }
    char chunk[65536];
    ssize_t got;
    const char *failed = NULL;
    while (!failed && (got = read(in, chunk, sizeof(chunk))) != 0) {
        if (got < 0) { if (errno != EINTR) failed = from; continue; }
        for (ssize_t done = 0, w; done < got && !failed; done += w > 0 ? w : 0)
            if ((w = write(out, chunk + done, (size_t)(got - done))) < 0 && errno != EINTR) failed = to;
    }
    int saved = errno;
    close(in);
    if (close(out) && !failed) failed = to, saved = errno;
    errno = saved;
    return failed;
}

/* Copy from to to, which must not exist. On failure, *where is the path that
 * failed, malloc-owned. */
static int copy(const char *from, const char *to, char **where)
{
    struct stat info;
    if (lstat(from, &info)) { *where = cmdr_strndup(from, strlen(from)); return 1; }
    if (S_ISREG(info.st_mode)) {
        const char *failed = copy_file(from, to, info.st_mode);
        if (!failed) return 0;
        *where = cmdr_strndup(failed, strlen(failed));
        return 1;
    } else if (S_ISLNK(info.st_mode)) {
        char *target = cmdr_realloc(NULL, (size_t)info.st_size + 1, 1);
        ssize_t n = readlink(from, target, (size_t)info.st_size + 1);
        int failed = n < 0 || n > info.st_size;
        if (n > info.st_size) errno = EAGAIN; /* Changed while copying. */
        if (!failed) target[n] = '\0', failed = symlink(target, to);
        free(target);
        if (!failed) return 0;
    } else if (S_ISDIR(info.st_mode)) {
        /* Owner write and search while copying in, then the source's mode. */
        mode_t mode = info.st_mode & 0777;
        if (mkdir(to, mode | 0700)) { *where = cmdr_strndup(to, strlen(to)); return 1; }
        DIR *directory = opendir(from);
        char **names;
        size_t n;
        if (!directory || read_names(directory, &names, &n)) {
            *where = cmdr_strndup(from, strlen(from));
            return 1;
        }
        int status = 0;
        for (size_t i = 0; i < n && !status; ++i) {
            char *a = join(from, names[i]), *b = join(to, names[i]);
            status = copy(a, b, where);
            free(a);
            free(b);
        }
        free_names(names, n);
        if (status) return 1;
        if ((mode & 0700) != 0700 && chmod(to, mode)) { *where = cmdr_strndup(to, strlen(to)); return 1; }
        return 0;
    } else {
        errno = EINVAL;
        *where = cmdr_strndup(from, strlen(from));
        return 2;
    }
    *where = cmdr_strndup(to, strlen(to));
    return 1;
}

/* A file, link, or directory tree, to DESTINATION, which must not exist, as
 * Python's shutil.copytree(symlinks=True). */
COMMAND(copy_command)
{
    if (argc != 2) return cmdr_error(error, "usage: fs.copy SOURCE DESTINATION");
    const char *from = argv[0].text, *to = argv[1].text;
    struct stat info;
    if (lstat(from, &info)) return cmdr_error(error, "fs.copy %s: %s", from, strerror(errno));
    if (S_ISDIR(info.st_mode)) {
        /* Into itself, the walk would find each copy it makes. */
        char *parent = parent_of(to), *a = realpath(from, NULL), *b = realpath(parent, NULL);
        size_t n = a ? strlen(a) : 0;
        int inside = a && b && !strncmp(a, b, n) && (b[n] == '/' || !b[n] || !strcmp(a, "/"));
        free(parent);
        free(a);
        free(b);
        if (inside) return cmdr_error(error, "fs.copy %s %s: cannot copy a directory into itself", from, to);
    }
    char *where = NULL;
    int status = copy(from, to, &where);
    if (status == 2) cmdr_error(error, "fs.copy %s: not a file, directory, or link", where);
    else if (status) cmdr_error(error, "fs.copy %s: %s", where, strerror(errno));
    free(where);
    return status ? 1 : same(&argv[1], result);
}

/* Remove name in the directory at at, and anything below it, without
 * following links: a link is removed, not what it points to. */
static int remove_at(int at, const char *name, const char *path, char **error)
{
    if (!unlinkat(at, name, 0) || errno == ENOENT) return 0;
    int saved = errno; /* EISDIR on Linux, EPERM on macOS, for a directory. */
    DIR *directory = open_dir_at(at, name);
    if (!directory) {
        if (errno == ENOTDIR || errno == ELOOP) errno = saved;
        return errno == ENOENT ? 0 : cmdr_error(error, "fs.remove-tree %s: %s", path, strerror(errno));
    }
    int fd = dup(dirfd(directory));
    char **names;
    size_t n;
    if (fd < 0 || read_names(directory, &names, &n)) {
        if (fd >= 0) close(fd);
        return cmdr_error(error, "fs.remove-tree %s: %s", path, strerror(errno));
    }
    int status = 0;
    for (size_t i = 0; i < n && !status; ++i) {
        char *child = join(path, names[i]);
        status = remove_at(fd, names[i], child, error);
        free(child);
    }
    close(fd);
    free_names(names, n);
    if (!status && unlinkat(at, name, AT_REMOVEDIR) && errno != ENOENT)
        status = cmdr_error(error, "fs.remove-tree %s: %s", path, strerror(errno));
    return status;
}

/* As rm -r: PATH must exist. Refuses /, and a last component of . or ..
 * A link, even written with a trailing /, is removed itself. */
COMMAND(remove_tree_command)
{
    if (argc != 1) return cmdr_error(error, "usage: fs.remove-tree PATH");
    const char *path = argv[0].text;
    size_t n = strlen(path);
    while (n && path[n - 1] == '/') --n;
    size_t start = n;
    while (start && path[start - 1] != '/') --start;
    if (!n || (n - start == 1 && path[start] == '.') || (n - start == 2 && !strncmp(path + start, "..", 2)))
        return cmdr_error(error, "fs.remove-tree: refusing to remove %s", *path ? path : "''");
    /* Without trailing slashes, which would resolve a link to a directory. */
    char *bare = cmdr_strndup(path, n);
    struct stat info;
    int status = lstat(bare, &info) ? cmdr_error(error, "fs.remove-tree %s: %s", path, strerror(errno))
                                    : remove_at(AT_FDCWD, bare, bare, error);
    free(bare);
    return status ? 1 : same(&argv[0], result);
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
    TEXT("is-link", "is-link PATH", "Return PATH if it is a symbolic link", is_link_command),
    TEXT("walk", "walk [DIRECTORY]", "List every path below DIRECTORY, sorted, not following links", walk_command),
    TEXT("symlink", "symlink TARGET PATH", "Create a symbolic link at PATH to TARGET", symlink_command),
    TEXT("realpath", "realpath PATH", "Return the absolute path, with links resolved", realpath_command),
    TEXT("temp-file", "temp-file [TEMPLATE]", "Create a new empty file; return its path", temp_file_command),
    TEXT("temp-dir", "temp-dir [TEMPLATE]", "Create a new directory; return its path", temp_dir_command),
    TEXT("copy", "copy SOURCE DESTINATION", "Copy a file, link, or tree to a new DESTINATION", copy_command),
    TEXT("remove-tree", "remove-tree PATH", "Delete PATH and everything below it", remove_tree_command),
};
const cmdr cmdr_ext_fs = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
