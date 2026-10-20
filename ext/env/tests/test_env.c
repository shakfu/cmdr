/* What test.cmdr cannot check: an empty variable is set, and the messages. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_env;

int main(void)
{
    const cmdr *table = ext_commands("env", &cmdr_ext_env);
    CHECK(!setenv("CMDR_TEST_ENV", "", 1));
    expect(table, "env.get CMDR_TEST_ENV | length", "0");
    CHECK(!unsetenv("CMDR_TEST_ENV"));
    expect_error(table, "env.get CMDR_TEST_ENV", CMDR_MISMATCH, "env.get: CMDR_TEST_ENV is not set");
    expect_error(table, "env.get A B", 1, "usage: env.get NAME");
    puts("test_env: ok");
    return 0;
}
