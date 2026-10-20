#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* Longer output is more likely a runaway format than a date. */
#define FORMAT_LIMIT 65536

COMMAND(now_command)
{
    if (argc) return cmdr_error(error, "usage: time.now");
    char text[32];
    snprintf(text, sizeof(text), "%lld", (long long)time(NULL));
    *result = cmdr_text(text, strlen(text));
    return 0;
}

/* strftime in local time or UTC: [EPOCH] FORMAT, so a piped time comes first. */
static int format(int utc, const char *name, size_t argc, const cmdr_value *argv,
                  cmdr_value *result, char **error)
{
    if (argc < 1 || argc > 2) return cmdr_error(error, "usage: %s [EPOCH] FORMAT", name);
    time_t when = time(NULL);
    if (argc == 2) {
        double x;
        if (cmdr_number(argv[0].text, &x, error)) return 1;
        if (x != floor(x))
            return cmdr_error(error, "%s: EPOCH must be an integer: %s", name, argv[0].text);
        /* 2^62 fits time_t on every platform with 64-bit time. */
        if (fabs(x) > 4611686018427387904.0)
            return cmdr_error(error, "%s: EPOCH out of range: %s", name, argv[0].text);
        when = (time_t)x;
    }
    struct tm parts;
    if (!utc) tzset(); /* localtime_r need not read TZ itself. */
    if (!(utc ? gmtime_r(&when, &parts) : localtime_r(&when, &parts)))
        return cmdr_error(error, "%s: EPOCH out of range: %lld", name, (long long)when);
    /* A trailing byte makes a successful result nonempty, since strftime
     * returns 0 both for empty output and for a buffer too small. */
    const char *spec = argv[argc - 1].text;
    size_t n = strlen(spec);
    char *marked = cmdr_realloc(NULL, n + 2, 1);
    memcpy(marked, spec, n);
    memcpy(marked + n, "x", 2);
    char *text = NULL;
    size_t written = 0;
    for (size_t size = 64; size <= FORMAT_LIMIT && !written; size *= 2) {
        text = cmdr_realloc(text, size, 1);
        written = strftime(text, size, marked, &parts);
    }
    free(marked);
    if (!written) {
        free(text);
        return cmdr_error(error, "%s: output exceeds %d bytes", name, FORMAT_LIMIT);
    }
    text[written - 1] = '\0';
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = text};
    return 0;
}
COMMAND(local_command) { return format(0, "time.local", argc, argv, result, error); }
COMMAND(utc_command) { return format(1, "time.utc", argc, argv, result, error); }

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    TEXT("now", "now", "Return the time in epoch seconds", now_command),
    TEXT("local", "local [EPOCH] FORMAT", "Format a time with strftime, in local time", local_command),
    TEXT("utc", "utc [EPOCH] FORMAT", "Format a time with strftime, in UTC", utc_command),
};
const cmdr cmdr_ext_time = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
