/* Messages, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_glob;

int main(void)
{
    const cmdr *table = ext_commands("glob", &cmdr_ext_glob);
    expect_error(table, "glob.glob 'src/**/*.c'", 1, "glob.glob: ** is reserved for recursive matching");
    expect_error(table, "glob.glob a**", 1, "glob.glob: ** is reserved");
    expect_error(table, "glob.glob a b", 1, "usage: glob.glob PATTERN");
    puts("test_glob: ok");
    return 0;
}
