/* Checks for extension test programs, which define _POSIX_C_SOURCE 200809L
 * and include cmdr.h first. CMake puts tests/ on their include path. */
#ifndef CMDR_EXT_CHECK_H
#define CMDR_EXT_CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
        abort(); \
    } \
} while (0)

static cmdr_session *ext_session;

static void ext_close(void) { cmdr_session_close(ext_session); }

/* An extension's table, bound as import _NAME as NAME binds it, in a session
 * closed at exit so LeakSanitizer reports only real leaks. */
static inline const cmdr *ext_commands(const char *name, const cmdr *table)
{
    cmdr_session *session = ext_session = cmdr_session_open(&cmdr_primitives, 1, NULL);
    atexit(ext_close);
    char module[64], line[160];
    snprintf(module, sizeof(module), "_%s", name);
    snprintf(line, sizeof(line), "import %s as %s", module, name);
    cmdr_session_add_module(session, module, table);
    cmdr_value value;
    char *error = NULL;
    if (cmdr_session_eval(session, line, &value, &error)) fprintf(stderr, "%s: %s\n", line, error);
    CHECK(!error);
    return cmdr_session_commands(session);
}

/* Evaluate source, which must succeed and render as printed. */
static inline void expect(const cmdr *repl, const char *source, const char *printed)
{
    cmdr_value value;
    char *error = NULL, *text = NULL;
    size_t size = 0;
    int status = cmdr_eval(repl, source, &value, &error);
    if (status) fprintf(stderr, "%s: %s\n", source, error ? error : "failed");
    CHECK(!status);
    FILE *out = open_memstream(&text, &size);
    CHECK(out);
    cmdr_render(&value, out);
    fclose(out);
    if (strcmp(text, printed)) fprintf(stderr, "%s: got '%s', want '%s'\n", source, text, printed);
    CHECK(!strcmp(text, printed));
    cmdr_free(&value);
    free(text);
    free(error);
}

/* Evaluate source, which must fail with status and an error containing message. */
static inline void expect_error(const cmdr *repl, const char *source, int status, const char *message)
{
    cmdr_value value;
    char *error = NULL;
    int got = cmdr_eval(repl, source, &value, &error);
    if (got != status || !error || !strstr(error, message))
        fprintf(stderr, "%s: got %d '%s', want %d '%s'\n", source, got,
                error ? error : "", status, message);
    CHECK(got == status && value.kind == CMDR_NONE && error && strstr(error, message));
    free(error);
}

#endif
