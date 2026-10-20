/* Text commands that work on bytes, as take and drop do. */
#include "cmdr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

/* A result larger than this is more likely a typo than text. */
#define SIZE_LIMIT ((size_t)64 << 20)

COMMAND(index_command)
{
    if (argc != 2) return cmdr_error(error, "usage: str.index TEXT SUBSTRING");
    const char *found = strstr(argv[0].text, argv[1].text);
    if (!found) {
        char *a = cmdr_describe(&argv[1]), *b = cmdr_describe(&argv[0]);
        cmdr_error(error, "str.index: %s not found in %s", a, b);
        free(a);
        free(b);
        return CMDR_MISMATCH;
    }
    char text[32];
    snprintf(text, sizeof(text), "%zu", (size_t)(found - argv[0].text));
    *result = cmdr_text(text, strlen(text));
    return 0;
}

static int limit(const char *name, size_t size, char **error)
{
    return size > SIZE_LIMIT ? cmdr_error(error, "%s: result exceeds %zu bytes", name, SIZE_LIMIT) : 0;
}

COMMAND(repeat_command)
{
    if (argc != 2) return cmdr_error(error, "usage: str.repeat TEXT N");
    size_t n = strlen(argv[0].text), count;
    if (cmdr_natural(&argv[1], &count, error)) return 1;
    /* Any count of empty text is empty; the loop below would still run count times. */
    if (!n) count = 0;
    /* Compare by division, so n * count cannot overflow. */
    if (n && count > SIZE_LIMIT / n) return limit("str.repeat", SIZE_LIMIT + 1, error);
    char *text = cmdr_realloc(NULL, n * count + 1, 1);
    for (size_t i = 0; i < count; ++i) memcpy(text + i * n, argv[0].text, n);
    text[n * count] = '\0';
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = text};
    return 0;
}

static int pad(int left, const char *name, size_t argc, const cmdr_value *argv,
               cmdr_value *result, char **error)
{
    if (argc < 2 || argc > 3) return cmdr_error(error, "usage: %s TEXT WIDTH [FILL]", name);
    size_t n = strlen(argv[0].text), width;
    if (cmdr_natural(&argv[1], &width, error)) return 1;
    if (argc == 3 && strlen(argv[2].text) != 1)
        return cmdr_error(error, "%s: FILL must be one byte", name);
    if (limit(name, width, error)) return 1;
    char fill = argc == 3 ? argv[2].text[0] : ' ';
    size_t extra = width > n ? width - n : 0;
    char *text = cmdr_realloc(NULL, n + extra + 1, 1);
    memset(left ? text : text + n, fill, extra);
    memcpy(left ? text + extra : text, argv[0].text, n);
    text[n + extra] = '\0';
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = text};
    return 0;
}
COMMAND(pad_left_command) { return pad(1, "str.pad-left", argc, argv, result, error); }
COMMAND(pad_right_command) { return pad(0, "str.pad-right", argc, argv, result, error); }

COMMAND(chars_command)
{
    if (argc != 1) return cmdr_error(error, "usage: str.chars TEXT");
    *result = (cmdr_value){.kind = CMDR_LIST};
    for (const char *p = argv[0].text; *p; ++p) cmdr_push(result, cmdr_text(p, 1));
    return 0;
}

/* Bytes of text, or items of a list. Text is not split into lines. */
COMMAND(reverse_command)
{
    if (argc != 1 || argv[0].kind == CMDR_BLOCK || argv[0].kind == CMDR_NONE)
        return cmdr_error(error, "usage: str.reverse LIST_OR_TEXT");
    if (argv[0].kind == CMDR_TEXT) {
        size_t n = strlen(argv[0].text);
        *result = cmdr_text(argv[0].text, n);
        for (size_t i = 0; i < n / 2; ++i) {
            char c = result->text[i];
            result->text[i] = result->text[n - 1 - i];
            result->text[n - 1 - i] = c;
        }
        return 0;
    }
    cmdr_value list = {.kind = CMDR_LIST};
    for (size_t i = argv[0].count; i--;) {
        cmdr_value item;
        if (cmdr_clone(&argv[0].items[i], &item, error)) { cmdr_free(&list); return 1; }
        cmdr_push(&list, item);
    }
    *result = list;
    return 0;
}

#define TEXT(n, u, d, f) {.name = n, .usage = u, .docstring = d, .run = f, .text = 1}
static const cmdr_command commands[] = {
    TEXT("index", "index TEXT SUBSTRING", "Return the byte offset of SUBSTRING", index_command),
    TEXT("repeat", "repeat TEXT N", "Repeat TEXT N times", repeat_command),
    TEXT("pad-left", "pad-left TEXT WIDTH [FILL]", "Pad on the left to WIDTH bytes", pad_left_command),
    TEXT("pad-right", "pad-right TEXT WIDTH [FILL]", "Pad on the right to WIDTH bytes", pad_right_command),
    TEXT("chars", "chars TEXT", "List the bytes of TEXT", chars_command),
    {.name = "reverse", .usage = "reverse LIST_OR_TEXT",
     .docstring = "Reverse the items of a list or the bytes of text", .run = reverse_command},
};
const cmdr cmdr_ext_str = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
