/* Commands that change files, in a temporary directory, and messages. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE /* macOS declares mkdtemp only outside strict POSIX. */
#include "cmdr.h"
#include "ext_check.h"

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
    CHECK(!rmdir(root));
    puts("test_fs: ok");
    return 0;
}
