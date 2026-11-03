/* Messages, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_hashlib;

int main(void)
{
    const cmdr *table = ext_commands("hashlib", &cmdr_ext_hashlib);
    expect_error(table, "hashlib.sha256-file /nonexistent/x", 1, "hashlib.sha256-file /nonexistent/x: No such file");
    expect_error(table, "hashlib.sha256 a b", 1, "usage: hashlib.sha256 TEXT");
    puts("test_hashlib: ok");
    return 0;
}
