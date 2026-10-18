/* What test.cmdr cannot check: an empty variable is set, and the messages. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_env;

int main(void)
{
    const cmdr table = {cmdr_ext_env.commands, cmdr_ext_env.count, &cmdr_primitives};
    CHECK(!setenv("CMDR_TEST_ENV", "", 1));
    expect(&table, "env CMDR_TEST_ENV | length", "0");
    CHECK(!unsetenv("CMDR_TEST_ENV"));
    expect_error(&table, "env CMDR_TEST_ENV", CMDR_MISMATCH, "env: CMDR_TEST_ENV is not set");
    expect_error(&table, "env A B", 1, "usage: env NAME");
    puts("test_env: ok");
    return 0;
}
