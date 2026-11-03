/* What print and tee return and write to stdout, and usage errors. Reading
 * stdin and writing stderr are in tests/test_cmdr.py. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

#include <unistd.h>

extern const cmdr cmdr_ext_io;

int main(void)
{
    const cmdr *table = ext_commands("io", &cmdr_ext_io);
    char path[] = "/tmp/cmdr-io-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0 && !close(fd));
    CHECK(freopen(path, "w", stdout));
    expect(table, "io.tee abc", "abc");
    expect(table, "io.print abc", "");
    expect(table, "list a b | io.tee | count", "2");
    expect(table, "io.print ''", "");
    expect(table, "list x y | each { (s) io.tee $s } | join ,", "x,y");
    expect_error(table, "io.print", 1, "usage: io.print VALUE");
    expect_error(table, "io.tee a b", 1, "usage: io.tee VALUE");
    expect_error(table, "io.input a b", 1, "usage: io.input [PROMPT]");
    /* each needs a value from its block. */
    expect_error(table, "list x | each { (s) io.print $s }", 1, "block returned no value");
    fflush(stdout);
    FILE *in = fopen(path, "r");
    CHECK(in);
    char written[64] = {0};
    fread(written, 1, sizeof(written) - 1, in);
    fclose(in);
    remove(path);
    if (strcmp(written, "abc\nabc\na\nb\n\nx\ny\nx\n"))
        fprintf(stderr, "stdout: got '%s'\n", written);
    CHECK(!strcmp(written, "abc\nabc\na\nb\n\nx\ny\nx\n"));
    return 0;
}
