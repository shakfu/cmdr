/* Time zones, limits, and messages, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

#include <signal.h>
#include <sys/time.h>
#include <time.h>

extern const cmdr cmdr_ext_time;

static double monotonic(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void on_alarm(int signal) { cmdr_interrupt(1); }

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

    double start = monotonic();
    expect(table, "time.sleep 0.12", "");
    CHECK(monotonic() - start >= 0.12);
    expect(table, "time.sleep 0", "");
    expect_error(table, "time.sleep -1", 1, "time.sleep: SECONDS -1 is below 0");
    expect_error(table, "time.sleep x", 1, "invalid finite number: x");
    expect_error(table, "time.sleep", 1, "usage: time.sleep SECONDS");
    /* An interrupt during the wait ends it. */
    struct sigaction action = {.sa_handler = on_alarm};
    CHECK(!sigaction(SIGALRM, &action, NULL));
    CHECK(!setitimer(ITIMER_REAL, &(struct itimerval){.it_value = {0, 100000}}, NULL));
    start = monotonic();
    expect_error(table, "time.sleep 60", 1, "time.sleep: interrupted");
    CHECK(monotonic() - start < 5);
    cmdr_interrupt(0);
    puts("test_time: ok");
    return 0;
}
