/* Error messages and the length limit, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_num;

int main(void)
{
    const cmdr table = {cmdr_ext_num.commands, cmdr_ext_num.count, &cmdr_primitives};
    expect_error(&table, "num-seq 1 10 0", 1, "num-seq: STEP must be nonzero");
    expect_error(&table, "num-seq 1 1e12", 1, "num-seq: 1000000000000 items exceeds 10000000");
    expect_error(&table, "num-seq 0 1e300 1e-300", 1, "num-seq: too many items");
    expect_error(&table, "num-seq a 3", 1, "invalid finite number: a");
    expect_error(&table, "num-seq 1", 1, "usage: num-seq FIRST LAST [STEP]");
    expect_error(&table, "num-mod 1 0", 1, "num-mod: division by zero");
    expect_error(&table, "num-floor x", 1, "invalid finite number: x");
    /* One past the limit fails before allocating; at the limit would build 10^7 items. */
    expect_error(&table, "num-seq 1 10000001", 1, "num-seq: 10000001 items exceeds 10000000");
    puts("test_num: ok");
    return 0;
}
