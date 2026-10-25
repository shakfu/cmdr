/* Messages, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_fnmatch;

int main(void)
{
    const cmdr *table = ext_commands("fnmatch", &cmdr_ext_fnmatch);
    expect_error(table, "fnmatch.match a.c '*.h'", CMDR_MISMATCH, "fnmatch.match: a.c does not match *.h");
    expect_error(table, "fnmatch.match a", 1, "usage: fnmatch.match NAME PATTERN");
    puts("test_fnmatch: ok");
    return 0;
}
