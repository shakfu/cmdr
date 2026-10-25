/* Commands that change files, in a temporary directory, and messages. */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700 /* realpath is XSI. */
#define _DARWIN_C_SOURCE /* macOS declares mkdtemp only outside strict POSIX. */
#include "cmdr.h"
#include "ext_check.h"

#include <sys/stat.h>
#include <unistd.h>

extern const cmdr cmdr_ext_fs;

static char root[] = "/tmp/cmdr-fs-XXXXXX";

/* format with each %s replaced by root. Buffers rotate, so one call may
 * pass two results. */
static const char *at(const char *format)
{
    static char lines[4][512];
    static int next;
    char *line = lines[next++ % 4], *out = line;
    for (const char *p = format; *p; ++p)
        if (p[0] == '%' && p[1] == 's') { out += sprintf(out, "%s", root); ++p; }
        else *out++ = *p;
    *out = '\0';
    return line;
}

int main(void)
{
    const cmdr *table = ext_commands("fs", &cmdr_ext_fs);
    CHECK(mkdtemp(root));
    expect(table, at("fs.mkdir %s/a/b/c"), at("%s/a/b/c"));
    expect(table, at("fs.mkdir %s/a/b/"), at("%s/a/b/")); /* Existing is fine. */
    expect(table, at("fs.is-dir %s/a/b/c"), at("%s/a/b/c"));
    expect(table, at("write hello %s/a/f.txt"), "hello");
    expect(table, at("fs.size %s/a/f.txt"), "5");
    expect(table, at("fs.dirs %s/a"), at("%s/a/b\n"));
    expect(table, at("fs.dirs %s/a/"), at("%s/a/b\n"));
    /* Links to directories are not listed, so a link to . cannot loop a walk. */
    CHECK(!symlink(".", at("%s/a/self")));
    expect(table, at("fs.dirs %s/a"), at("%s/a/b\n"));
    CHECK(!unlink(at("%s/a/self")));
    expect_error(table, at("fs.mkdir %s/a/f.txt/x"), 1, "Not a directory");
    expect_error(table, at("fs.mkdir %s/a/f.txt"), 1, "Not a directory");
    expect_error(table, at("fs.is-dir %s/a/f.txt"), CMDR_MISMATCH, "is not a directory");
    expect_error(table, at("fs.is-file %s/a"), CMDR_MISMATCH, "is not a regular file");
    expect_error(table, at("fs.exists %s/none"), CMDR_MISMATCH, "does not exist");
    expect_error(table, at("fs.is-file %s/none/x"), CMDR_MISMATCH, "does not exist");
    /* fs.move replaces the destination. */
    expect(table, at("write other %s/g.txt"), "other");
    expect(table, at("fs.move %s/a/f.txt %s/g.txt"), at("%s/g.txt"));
    expect(table, at("read %s/g.txt"), "hello");
    expect_error(table, at("fs.exists %s/a/f.txt"), CMDR_MISMATCH, "does not exist");
    /* fs.remove is not recursive. */
    expect_error(table, at("fs.remove %s/a"), 1, at("fs.remove %s/a: "));
    expect(table, at("fs.remove %s/g.txt"), at("%s/g.txt"));
    expect(table, at("fs.remove %s/a/b/c"), at("%s/a/b/c"));
    expect(table, at("fs.remove %s/a/b"), at("%s/a/b"));
    expect(table, at("fs.remove %s/a"), at("%s/a"));
    expect(table, at("fs.dirs %s | count"), "0");
    expect_error(table, at("fs.remove %s/none"), 1, "No such file or directory");
    expect_error(table, at("fs.size %s/none"), 1, "No such file or directory");
    expect_error(table, at("fs.dirs %s/none"), 1, "No such file or directory");
    expect_error(table, "fs.mkdir ''", 1, "fs.mkdir: empty path");
    expect_error(table, "fs.move a", 1, "usage: fs.move SOURCE DESTINATION");

    /* Temporary paths. */
    umask(022);
    CHECK(!setenv("TMPDIR", root, 1));
    expect(table, at("fs.temp-dir | contains %s/cmdr- | fs.is-dir | fs.remove | count"), "1");
    expect(table, at("fs.temp-file | contains %s/cmdr- | fs.is-file | fs.size"), "0");
    expect(table, at("fs.temp-dir %s/t-XXXXXX | not { contains XXXXXX } | contains %s/t- | fs.remove | count"), "1");
    expect(table, at("fs.temp-file %s/t-XXXXXX | not { contains XXXXXX } | fs.remove | count"), "1");
    expect(table, at("fs.walk %s | count"), "1"); /* The cmdr- file. */
    expect(table, at("fs.walk %s | join '' | fs.remove | count"), "1");
    expect_error(table, "fs.temp-dir abc", 1, "fs.temp-dir: TEMPLATE must end in XXXXXX: abc");
    expect_error(table, at("fs.temp-file %s/none/t-XXXXXX"), 1, "No such file or directory");

    /* Links. */
    expect(table, at("fs.mkdir %s/a/b/c"), at("%s/a/b/c"));
    expect(table, at("fs.mkdir %s/a/b-x"), at("%s/a/b-x"));
    expect(table, at("write hello %s/a/f.txt"), "hello");
    expect(table, at("write g %s/a/b/g.txt"), "g");
    expect(table, at("fs.symlink f.txt %s/a/l"), at("%s/a/l"));
    expect(table, at("fs.is-link %s/a/l"), at("%s/a/l"));
    expect(table, at("read %s/a/l"), "hello");
    expect_error(table, at("fs.is-link %s/a/f.txt"), CMDR_MISMATCH, "is not a symbolic link");
    expect_error(table, at("fs.is-link %s/none"), CMDR_MISMATCH, "does not exist");
    expect_error(table, at("fs.symlink f.txt %s/a/l"), 1, "File exists");
    expect(table, at("fs.symlink .. %s/a/up"), at("%s/a/up"));
    char *real = realpath(root, NULL), line[600], want[600];
    CHECK(real);
    snprintf(line, sizeof(line), "fs.realpath %s/a/l", root);
    snprintf(want, sizeof(want), "%s/a/f.txt", real);
    expect(table, line, want);
    snprintf(line, sizeof(line), "fs.realpath %s/a/up/a/b/../b", root);
    snprintf(want, sizeof(want), "%s/a/b", real);
    expect(table, line, want);
    expect_error(table, at("fs.realpath %s/none"), 1, "No such file or directory");

    /* fs.walk sorts by path, so b-x comes before b/c, and lists links without
     * following them. */
    expect(table, at("fs.walk %s/a"),
           at("%s/a/b\n%s/a/b-x\n%s/a/b/c\n%s/a/b/g.txt\n%s/a/f.txt\n%s/a/l\n%s/a/up\n"));
    expect(table, at("fs.walk %s/a/b/c | count"), "0");
    expect_error(table, at("fs.walk %s/a/f.txt"), 1, "Not a directory");

    /* fs.copy copies links as links, and refuses an existing destination. */
    CHECK(!chmod(at("%s/a/f.txt"), 0750));
    expect(table, at("fs.copy %s/a %s/copy"), at("%s/copy"));
    expect(table, at("fs.walk %s/copy | count"), "7");
    expect(table, at("fs.is-link %s/copy/up"), at("%s/copy/up"));
    expect(table, at("read %s/copy/l"), "hello");
    expect(table, at("read %s/copy/b/g.txt"), "g");
    struct stat info;
    CHECK(!stat(at("%s/copy/f.txt"), &info) && (info.st_mode & 0777) == 0750);
    expect_error(table, at("fs.copy %s/a %s/copy"), 1, at("fs.copy %s/copy: File exists"));
    expect_error(table, at("fs.copy %s/a/f.txt %s/copy/l"), 1, "File exists");
    expect_error(table, at("fs.copy %s/a %s/a/b/inner"), 1, "cannot copy a directory into itself");
    expect_error(table, at("fs.copy %s/a %s/a/"), 1, "File exists");
    expect_error(table, at("fs.copy %s/none %s/x"), 1, "No such file or directory");
    expect(table, at("fs.copy %s/a/f.txt %s/f2 | read"), "hello");
    CHECK(!mkfifo(at("%s/fifo"), 0600));
    expect_error(table, at("fs.copy %s/fifo %s/x"), 1, at("fs.copy %s/fifo: not a file, directory, or link"));
    /* A read-only directory is copied in, then given its mode. */
    expect(table, at("fs.mkdir %s/ro"), at("%s/ro"));
    expect(table, at("write r %s/ro/r.txt"), "r");
    CHECK(!chmod(at("%s/ro"), 0555));
    expect(table, at("fs.copy %s/ro %s/ro2 | fs.walk"), at("%s/ro2/r.txt\n"));
    CHECK(!stat(at("%s/ro2"), &info) && (info.st_mode & 0777) == 0555);

    /* fs.remove-tree removes links, not what they point to. */
    if (geteuid()) /* root may unlink in a read-only directory. */
        expect_error(table, at("fs.remove-tree %s/ro2"), 1, at("fs.remove-tree %s/ro2/r.txt: Permission denied"));
    CHECK(!chmod(at("%s/ro"), 0755) && !chmod(at("%s/ro2"), 0755));
    expect(table, at("fs.symlink %s/a %s/copy/outside"), at("%s/copy/outside"));
    expect(table, at("fs.remove-tree %s/copy"), at("%s/copy"));
    expect_error(table, at("fs.exists %s/copy"), CMDR_MISMATCH, "does not exist");
    expect(table, at("fs.walk %s/a | count"), "7");
    expect(table, at("fs.symlink a %s/la"), at("%s/la"));
    expect(table, at("fs.remove-tree %s/la/"), at("%s/la/"));
    expect(table, at("fs.walk %s/a | count"), "7");
    expect_error(table, "fs.remove-tree .", 1, "fs.remove-tree: refusing to remove .");
    expect_error(table, "fs.remove-tree ./", 1, "refusing to remove ./");
    expect_error(table, at("fs.remove-tree %s/a/.."), 1, "refusing to remove");
    expect_error(table, at("fs.remove-tree %s/a/./"), 1, "refusing to remove");
    expect_error(table, "fs.remove-tree /", 1, "refusing to remove /");
    expect_error(table, "fs.remove-tree //", 1, "refusing to remove //");
    expect_error(table, "fs.remove-tree ''", 1, "refusing to remove ''");
    expect_error(table, at("fs.remove-tree %s/none"), 1, "No such file or directory");
    expect(table, at("list a ro ro2 f2 fifo | each { (p) fs.remove-tree $(concat %s/ $p) } | count"), "5");
    expect(table, at("fs.walk %s | count"), "0");
    free(real);
    CHECK(!rmdir(root));
    puts("test_fs: ok");
    return 0;
}
