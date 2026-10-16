#define _POSIX_C_SOURCE 200809L
#include "commands.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int text_command(void *user, size_t argc, const char *const *argv,
                        char **result, char **error)
{
    (void)user;
    size_t size = 1;
    for (size_t i = 0; i < argc; ++i) size += strlen(argv[i]) + (i != 0);
    *result = malloc(size);
    if (!*result) return cmdr_error(error, "out of memory");
    char *p = *result;
    for (size_t i = 0; i < argc; ++i) {
        if (i) *p++ = ' ';
        size_t n = strlen(argv[i]);
        memcpy(p, argv[i], n);
        p += n;
    }
    *p = '\0';
    return 0;
}

static int upper_command(void *user, size_t argc, const char *const *argv,
                         char **result, char **error)
{
    (void)user;
    if (argc != 1) return cmdr_error(error, "usage: upper TEXT");
    *result = strdup(argv[0]);
    if (!*result) return cmdr_error(error, "out of memory");
    for (char *p = *result; *p; ++p) *p = (char)toupper((unsigned char)*p);
    return 0;
}

static int read_command(void *user, size_t argc, const char *const *argv,
                        char **result, char **error)
{
    (void)user;
    if (argc != 1) return cmdr_error(error, "usage: read PATH");
    FILE *file = fopen(argv[0], "rb");
    if (!file) return cmdr_error(error, "read %s: %s", argv[0], strerror(errno));
    size_t used = 0, capacity = 4096;
    char *data = malloc(capacity);
    if (!data) { fclose(file); return cmdr_error(error, "out of memory"); }
    int status = 0;
    for (;;) {
        size_t n = fread(data + used, 1, capacity - used - 1, file);
        if (memchr(data + used, '\0', n)) {
            status = cmdr_error(error, "read: binary data is not supported");
            break;
        }
        used += n;
        if (ferror(file)) { status = cmdr_error(error, "read %s failed", argv[0]); break; }
        if (feof(file)) break;
        if (capacity > (size_t)-1 / 2) {
            status = cmdr_error(error, "file is too large");
            break;
        }
        capacity *= 2;
        char *next = realloc(data, capacity);
        if (!next) { status = cmdr_error(error, "out of memory"); break; }
        data = next;
    }
    fclose(file);
    if (status) free(data);
    else { data[used] = '\0'; *result = data; }
    return status;
}

static int write_command(void *user, size_t argc, const char *const *argv,
                         char **result, char **error)
{
    (void)user;
    if (argc != 2) return cmdr_error(error, "usage: write TEXT PATH");
    char *copy = strdup(argv[0]);
    if (!copy) return cmdr_error(error, "out of memory");
    FILE *file = fopen(argv[1], "wb");
    if (!file) { free(copy); return cmdr_error(error, "write %s: %s", argv[1], strerror(errno)); }
    size_t n = strlen(copy);
    int failed = fwrite(copy, 1, n, file) != n;
    if (fclose(file)) failed = 1;
    if (failed) { free(copy); return cmdr_error(error, "write %s failed", argv[1]); }
    *result = copy;
    return 0;
}

static int help_command(void *user, size_t argc, const char *const *argv,
                        char **result, char **error)
{
    (void)user;
    (void)argv;
    (void)result;
    if (argc) return cmdr_error(error, "usage: help");
    cmdr repl;
    cmdr_commands_init(&repl);
    for (size_t i = 0; i < repl.count; ++i) puts(repl.commands[i].usage);
    puts("Compose with |; the returned value becomes the next command's first argument.");
    puts("Use single/double quotes or backslash escapes for spaces and literal pipes.");
    return ferror(stdout) ? cmdr_error(error, "output failed") : 0;
}

void cmdr_commands_init(cmdr *repl)
{
    static const cmdr_command commands[] = {
        {"text", "text [ARG ...]     Join arguments with spaces", text_command, NULL},
        {"upper", "upper TEXT         Convert text to uppercase", upper_command, NULL},
        {"read", "read PATH          Read a text file", read_command, NULL},
        {"write", "write TEXT PATH    Replace a file, returning TEXT", write_command, NULL},
        {"help", "help               Show commands", help_command, NULL},
    };
    *repl = (cmdr){commands, sizeof(commands) / sizeof(commands[0])};
}
