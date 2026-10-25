/* Text counted in code points, as Python's str counts it, where take, drop,
 * length, and str count bytes. */
#include "cmdr.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* The byte offset of the first invalid sequence in text, or -1. */
static long invalid_at(const char *text)
{
    const unsigned char *p = (const unsigned char *)text, *end = p + strlen(text);
    for (size_t n; p < end; p += n)
        if (!(n = utf8_length(p, end))) return (long)(p - (const unsigned char *)text);
    return -1;
}

static int check(const char *name, const char *text, char **error)
{
    long at = invalid_at(text);
    return at < 0 ? 0 : cmdr_error(error, "%s: invalid UTF-8 at byte %ld", name, at);
}

/* The byte offset of code point n, or the end. Text is valid. */
static size_t offset(const char *text, size_t n)
{
    const unsigned char *p = (const unsigned char *)text, *end = p + strlen(text);
    for (; n && p < end; --n) p += utf8_length(p, end);
    return (size_t)(p - (const unsigned char *)text);
}

COMMAND(length_command)
{
    if (argc != 1) return cmdr_error(error, "usage: utf8.length TEXT");
    if (check("utf8.length", argv[0].text, error)) return 1;
    size_t n = 0;
    for (const char *p = argv[0].text; *p; ++p) n += ((unsigned char)*p & 0xc0) != 0x80;
    char text[32];
    snprintf(text, sizeof(text), "%zu", n);
    *result = cmdr_text(text, strlen(text));
    return 0;
}

static int cut(int take, size_t argc, const cmdr_value *argv, cmdr_value *result, char **error)
{
    const char *name = take ? "utf8.take" : "utf8.drop";
    if (argc != 2) return cmdr_error(error, "usage: %s TEXT N", name);
    size_t n;
    if (cmdr_natural(&argv[1], &n, error) || check(name, argv[0].text, error)) return 1;
    const char *text = argv[0].text;
    size_t at = offset(text, n);
    *result = take ? cmdr_text(text, at) : cmdr_text(text + at, strlen(text + at));
    return 0;
}
COMMAND(take_command) { return cut(1, argc, argv, result, error); }
COMMAND(drop_command) { return cut(0, argc, argv, result, error); }

COMMAND(chars_command)
{
    if (argc != 1) return cmdr_error(error, "usage: utf8.chars TEXT");
    if (check("utf8.chars", argv[0].text, error)) return 1;
    const unsigned char *p = (const unsigned char *)argv[0].text, *end = p + strlen(argv[0].text);
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (size_t n; p < end; p += n) {
        n = utf8_length(p, end);
        cmdr_push(result, cmdr_text((const char *)p, n));
    }
    return 0;
}

COMMAND(valid_command)
{
    if (argc != 1) return cmdr_error(error, "usage: utf8.valid TEXT");
    if (check("utf8.valid", argv[0].text, error)) return CMDR_MISMATCH;
    *result = cmdr_text(argv[0].text, strlen(argv[0].text));
    return 0;
}

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    TEXT("length", "length TEXT", "Count code points", length_command),
    TEXT("take", "take TEXT N", "Take the first N code points", take_command),
    TEXT("drop", "drop TEXT N", "Drop the first N code points", drop_command),
    TEXT("chars", "chars TEXT", "List the code points, each as text", chars_command),
    TEXT("valid", "valid TEXT", "Return TEXT if it is valid UTF-8", valid_command),
};
const cmdr cmdr_ext_utf8 = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
