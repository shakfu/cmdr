/* Limits and messages, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_str;

int main(void)
{
    const cmdr table = {cmdr_ext_str.commands, cmdr_ext_str.count, &cmdr_primitives};
    expect_error(&table, "str-index hello z", CMDR_MISMATCH, "str-index: z not found in hello");
    expect_error(&table, "str-repeat ab 33554433", 1, "str-repeat: result exceeds 67108864 bytes");
    expect_error(&table, "str-repeat ab 18446744073709551615", 1, "result exceeds");
    expect(&table, "str-repeat ab 33554432 | length", "67108864");
    expect_error(&table, "str-repeat ab -1", 1, "invalid nonnegative integer: -1");
    expect_error(&table, "str-pad-left x 67108865", 1, "str-pad-left: result exceeds");
    expect_error(&table, "str-pad-left x 3 ab", 1, "str-pad-left: FILL must be one byte");
    expect_error(&table, "str-pad-right x 3 ''", 1, "str-pad-right: FILL must be one byte");
    expect_error(&table, "str-reverse { upper }", 1, "usage: str-reverse LIST_OR_TEXT");
    /* List items are cloned, including nested lists. */
    expect(&table, "list a b | with { (x) list $x c } | str-reverse | each { count } | join ,",
           "1,2");
    puts("test_str: ok");
    return 0;
}
