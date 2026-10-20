/* Time zones, limits, and messages, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_time;

int main(void)
{
    const cmdr *table = ext_commands("time", &cmdr_ext_time);
    CHECK(!setenv("TZ", "UTC", 1));
    expect(table, "time.local 0 %H", "00");
    CHECK(!setenv("TZ", "EST5", 1)); /* POSIX form: UTC-5, no daylight time. */
    expect(table, "time.local 0 '%Y-%m-%d %H'", "1969-12-31 19");
    expect(table, "time.utc 0 %H", "00");
    expect_error(table, "time.utc 1.5 %Y", 1, "time.utc: EPOCH must be an integer: 1.5");
    expect_error(table, "time.utc 1e300 %Y", 1, "time.utc: EPOCH out of range: 1e300");
    expect_error(table, "time.utc x %Y", 1, "invalid finite number: x");
    expect_error(table, "time.utc", 1, "usage: time.utc [EPOCH] FORMAT");
    expect_error(table, "time.now 1", 1, "usage: time.now");
    /* Each %Y prints 4 bytes, so 17500 of them print 70000, past the limit. */
    char *line = malloc(sizeof("time.utc 0 ") + 2 * 17500);
    CHECK(line);
    char *p = line + sprintf(line, "time.utc 0 ");
    for (int i = 0; i < 17500; ++i) p += sprintf(p, "%%Y");
    expect_error(table, line, 1, "time.utc: output exceeds 65536 bytes");
    free(line);
    puts("test_time: ok");
    return 0;
}
