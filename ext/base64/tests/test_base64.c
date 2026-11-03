/* Messages, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_base64;

int main(void)
{
    const cmdr *table = ext_commands("base64", &cmdr_ext_base64);
    expect_error(table, "base64.decode Zm9", 1, "base64.decode: incorrect padding");
    expect_error(table, "base64.decode Z===", 1, "base64.decode: incorrect padding");
    expect_error(table, "base64.decode Zg=a", 1, "base64.decode: incorrect padding");
    expect_error(table, "base64.decode Zg==Zg==", 1, "base64.decode: incorrect padding");
    expect_error(table, "base64.decode Zm9v!", 1, "base64.decode: invalid character at byte 5");
    expect_error(table, "base64.decode AAH/", 1, "base64.decode: the data holds a NUL byte");
    expect_error(table, "base64.encode-file /nonexistent/x", 1, "base64.encode-file /nonexistent/x: No such file");
    expect_error(table, "base64.encode a b", 1, "usage: base64.encode TEXT");
    puts("test_base64: ok");
    return 0;
}
