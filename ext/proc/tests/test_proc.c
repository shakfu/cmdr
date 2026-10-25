/* Messages, and a zombie: a child exited but not reaped. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern const cmdr cmdr_ext_proc;

int main(void)
{
    const cmdr *table = ext_commands("proc", &cmdr_ext_proc);
    char line[128], want[128];
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(0);
    /* Wait for the exit without reaping it. */
    for (int i = 0; i < 200; ++i) {
        siginfo_t info = {0};
        CHECK(!waitid(P_PID, (id_t)child, &info, WEXITED | WNOHANG | WNOWAIT));
        if (info.si_pid) break;
        nanosleep(&(struct timespec){0, 10000000}, NULL);
    }
    snprintf(line, sizeof(line), "proc.alive %d", (int)child);
    snprintf(want, sizeof(want), "proc.alive: process %d has exited", (int)child);
#if defined(__linux__) || defined(__APPLE__)
    expect_error(table, line, CMDR_MISMATCH, want);
#endif
    CHECK(waitpid(child, NULL, 0) == child);
    snprintf(want, sizeof(want), "proc.alive: no process %d", (int)child);
    expect_error(table, line, CMDR_MISMATCH, want);
    snprintf(line, sizeof(line), "proc.kill %d", (int)child);
    snprintf(want, sizeof(want), "proc.kill: no process %d", (int)child);
    expect_error(table, line, CMDR_MISMATCH, want);

    expect_error(table, "proc.alive x", 1, "proc.alive: PID is not an integer: x");
    expect_error(table, "proc.alive 1.5", 1, "PID is not an integer");
    expect_error(table, "proc.alive 99999999999", 1, "PID is not an integer");
    expect_error(table, "proc.alive -5", 1, "proc.alive: PID must be positive: -5");
    expect_error(table, "proc.alive 0", 1, "PID must be positive");
    expect_error(table, "proc.kill 0", 1, "proc.kill: refusing PID 0");
    expect_error(table, "proc.kill -1 0", 1, "proc.kill: refusing PID -1");
    expect_error(table, "proc.kill 1 BOGUS", 1, "proc.kill: unknown signal BOGUS");
    expect_error(table, "proc.kill 1 -9", 1, "unknown signal -9");
    expect_error(table, "proc.kill", 1, "usage: proc.kill PID [SIGNAL]");
    if (geteuid()) expect_error(table, "proc.kill 1 0", 1, "proc.kill 1: Operation not permitted");
    expect(table, "proc.alive 1", "1"); /* init: running, though not ours to signal. */
    puts("test_proc: ok");
    return 0;
}
