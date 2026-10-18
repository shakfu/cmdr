/* What test.cmdr cannot check: an empty variable is set, and the message. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
        abort(); \
    } \
} while (0)

extern const cmdr cmdr_ext_env;

int main(void)
{
    const cmdr table = {cmdr_ext_env.commands, cmdr_ext_env.count, &cmdr_primitives};
    cmdr_value value;
    char *error = NULL;
    CHECK(!setenv("CMDR_TEST_ENV", "", 1));
    CHECK(!cmdr_eval(&table, "env CMDR_TEST_ENV", &value, &error));
    CHECK(value.kind == CMDR_TEXT && !strcmp(value.text, ""));
    cmdr_free(&value);
    CHECK(!unsetenv("CMDR_TEST_ENV"));
    CHECK(cmdr_eval(&table, "env CMDR_TEST_ENV", &value, &error) == CMDR_MISMATCH);
    CHECK(value.kind == CMDR_NONE && strstr(error, "env: CMDR_TEST_ENV is not set"));
    free(error);
    error = NULL;
    CHECK(cmdr_eval(&table, "env A B", &value, &error) == 1);
    CHECK(strstr(error, "usage: env NAME"));
    free(error);
    puts("test_env: ok");
    return 0;
}
