/* Shell-style splitting and quoting, as Python's shlex does with posix=True. */
#include "cmdr.h"

#include <stdlib.h>
#include <string.h>

#define COMMAND(name) \
    static int name(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv, \
                    cmdr_value *result, char **error)

typedef struct {
    char *data;
    size_t length, capacity;
} buffer;

static void put(buffer *b, char c)
{
    if (b->length + 2 > b->capacity) {
        b->capacity = (b->length + 2) * 2;
        b->data = cmdr_realloc(b->data, b->capacity, 1);
    }
    b->data[b->length++] = c;
    b->data[b->length] = '\0';
}

static void put_text(buffer *b, const char *text)
{
    for (; *text; ++text) put(b, *text);
}

static int space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Words as a POSIX shell reads them, without expansion. Inside double quotes,
 * a backslash escapes only " and \, as in Python. */
COMMAND(split_command)
{
    if (argc != 1) return cmdr_error(error, "usage: shlex-split TEXT");
    const char *p = argv[0].text;
    *result = (cmdr_value){.kind = CMDR_LIST};
    buffer word = {0};
    int in_word = 0;
    for (;;) {
        char c = *p++;
        if (!c || space(c)) {
            if (in_word) cmdr_push(result, cmdr_text(word.data ? word.data : "", word.length));
            word.length = 0;
            in_word = 0;
            if (!c) break;
            continue;
        }
        in_word = 1;
        if (c == '\\') {
            if (!*p) { free(word.data); cmdr_free(result); return cmdr_error(error, "shlex-split: no escaped character"); }
            put(&word, *p++);
        } else if (c == '\'' || c == '"') {
            for (;;) {
                char q = *p++;
                if (!q) { free(word.data); cmdr_free(result); return cmdr_error(error, "shlex-split: no closing quotation"); }
                if (q == c) break;
                if (c == '"' && q == '\\' && (*p == '"' || *p == '\\')) q = *p++;
                put(&word, q);
            }
        } else {
            put(&word, c);
        }
    }
    free(word.data);
    return 0;
}

/* Bytes that need no quoting, as Python's shlex: [A-Za-z0-9_@%+=:,./-]. */
static int safe(const char *text)
{
    for (; *text; ++text)
        if (!strchr("@%+=:,./-_", *text) && !(*text >= 'a' && *text <= 'z')
            && !(*text >= 'A' && *text <= 'Z') && !(*text >= '0' && *text <= '9'))
            return 0;
    return 1;
}

/* Single quotes, with each ' written as '"'"'. */
static void quote(buffer *b, const char *text)
{
    if (*text && safe(text)) { put_text(b, text); return; }
    put(b, '\'');
    for (; *text; ++text)
        if (*text == '\'') put_text(b, "'\"'\"'");
        else put(b, *text);
    put(b, '\'');
}

COMMAND(quote_command)
{
    if (argc != 1) return cmdr_error(error, "usage: shlex-quote TEXT");
    buffer b = {0};
    quote(&b, argv[0].text);
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = b.data};
    return 0;
}

/* Text is split into lines, as list primitives do, never into words. */
COMMAND(join_command)
{
    if (argc != 1 || argv[0].kind == CMDR_BLOCK) return cmdr_error(error, "usage: shlex-join LIST_OR_LINES");
    cmdr_value lines = {0};
    if (argv[0].kind == CMDR_TEXT && cmdr_call(repl, "lines", 1, argv, &lines, error)) return 1;
    const cmdr_value *list = argv[0].kind == CMDR_TEXT ? &lines : &argv[0];
    buffer b = {0};
    int status = 0;
    for (size_t i = 0; i < list->count && !status; ++i) {
        const cmdr_value *item = &list->items[i];
        if (item->kind != CMDR_TEXT) { status = cmdr_error(error, "shlex-join: expected text items"); break; }
        if (i) put(&b, ' ');
        quote(&b, item->text);
    }
    cmdr_free(&lines);
    if (status) { free(b.data); return 1; }
    *result = (cmdr_value){.kind = CMDR_TEXT, .text = b.data ? b.data : cmdr_strndup("", 0)};
    return 0;
}

static const cmdr_command commands[] = {
    {.name = "shlex-split", .usage = "shlex-split TEXT",
     .docstring = "Split TEXT into words by POSIX shell quoting, without expansion",
     .run = split_command, .text = 1},
    {.name = "shlex-quote", .usage = "shlex-quote TEXT",
     .docstring = "Quote TEXT so a shell reads it as one word", .run = quote_command, .text = 1},
    {.name = "shlex-join", .usage = "shlex-join LIST_OR_LINES",
     .docstring = "Quote each item and join with spaces; shlex-split reverses it",
     .run = join_command},
};
const cmdr cmdr_ext_shlex = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
