/* Error messages, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_random;

int main(void)
{
    const cmdr table = {cmdr_ext_random.commands, cmdr_ext_random.count, &cmdr_primitives};
    expect_error(&table, "random-int 3 1", 1, "random-int: empty range 3 to 1");
    expect_error(&table, "random-int 1.5 3", 1, "random-int: A 1.5 is not an integer within 2^53");
    expect_error(&table, "random-seed 1e300", 1, "random-seed: N 1e300 is not an integer");
    expect_error(&table, "random-choice ''", 1, "random-choice: no items");
    expect_error(&table, "list | random-choices 1", 1, "random-choices: no items");
    expect_error(&table, "list a b | random-choices 1 '1'", 1,
                 "random-choices: 1 weights for 2 items");
    expect_error(&table, "list a b | random-choices 1 \"0\\n0\"", 1,
                 "random-choices: the weights total 0, not a positive number");
    expect_error(&table, "list a b | random-choices 1 \"x\\n0\"", 1, "invalid finite number: x");
    expect_error(&table, "list a b | random-choices -1", 1, "random-choices: K -1 is not a count");
    expect_error(&table, "list a b | random-sample 3", 1, "random-sample: K 3 exceeds 2 items");
    expect_error(&table, "random 1", 1, "usage: random");
    puts("test_random: ok");
    return 0;
}
