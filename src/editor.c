#define _POSIX_C_SOURCE 200809L
#include "editor.h"

#include "linenoise.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { COMMAND = 4, UNKNOWN = 1, STRING = 2, REFERENCE = 5, PIPE = 3, COMMENT = 6 };

struct editor {
    linenoise_context_t *ctx;
    const char *const *names; /* Borrowed during editor_read. */
    size_t count;
    char *history; /* NULL when $HOME is unset. */
};

typedef enum { WORD, REF, BAR, NOTE } token_kind;

typedef struct {
    token_kind kind;
    size_t start, end;
    int command; /* First word of a stage. */
    int plain;   /* No quotes or escapes, so the text is the word. */
} token;

/* Split buf[0, len) as parse in cmdr.janet does, calling visit per token and
 * marking quoted bytes in colors when non-NULL. */
static void scan(const char *buf, size_t len, char *colors,
                 void (*visit)(const token *, void *), void *data)
{
    size_t i = 0;
    while (i < len && isspace((unsigned char)buf[i])) ++i;
    if (i < len && buf[i] == '#') {
        visit(&(token){NOTE, i, len, 0, 0}, data);
        return;
    }
    int command = 1;
    while (i < len) {
        if (isspace((unsigned char)buf[i])) { ++i; continue; }
        token t = {WORD, i, i, command, 1};
        if (buf[i] == '|') {
            t.kind = BAR;
            t.end = ++i;
            command = 1;
            visit(&t, data);
            continue;
        }
        if (buf[i] == '$') {
            t.kind = REF;
            while (i < len && !isspace((unsigned char)buf[i]) && buf[i] != '|') ++i;
            t.end = i;
            command = 0;
            visit(&t, data);
            continue;
        }
        char quote = 0;
        while (i < len && (quote || (!isspace((unsigned char)buf[i]) && buf[i] != '|'))) {
            char c = buf[i];
            size_t width = 1;
            if (c == '\\' && quote != '\'') {
                width = i + 1 < len ? 2 : 1;
                t.plain = 0;
            } else if (quote && c == quote) {
                quote = 0;
            } else if (!quote && (c == '\'' || c == '"')) {
                quote = c;
                t.plain = 0;
            } else if (!quote) {
                i += width;
                continue;
            }
            if (colors && (quote || c == '\'' || c == '"' || c == '\\'))
                memset(colors + i, STRING, width);
            i += width;
        }
        t.end = i;
        command = 0;
        visit(&t, data);
    }
}

/* Whether name, of length n, is an active command. */
static int known(const editor *ed, const char *name, size_t n)
{
    for (size_t i = 0; i < ed->count; ++i)
        if (strlen(ed->names[i]) == n && !memcmp(ed->names[i], name, n)) return 1;
    return 0;
}

typedef struct {
    const editor *ed;
    const char *buf;
    char *colors;
    size_t pos;
} paint;

static void paint_token(const token *t, void *data)
{
    paint *p = data;
    size_t n = t->end - t->start;
    char *at = p->colors + t->start;
    if (t->kind == NOTE) memset(at, COMMENT, n);
    else if (t->kind == BAR) *at = PIPE;
    else if (t->kind == REF) memset(at, REFERENCE, n);
    else if (t->command && t->plain) {
        /* A word still being typed is not marked unknown. */
        if (known(p->ed, p->buf + t->start, n)) memset(at, COMMAND, n);
        else if (p->pos != t->end) memset(at, UNKNOWN, n);
    }
}

static void highlight(const char *buf, char *colors, size_t len, size_t pos, void *udata)
{
    paint p = {udata, buf, colors, pos};
    scan(buf, len, colors, paint_token, &p);
}

static void last(const token *t, void *data)
{
    *(token *)data = *t;
}

static void complete(const char *buf, size_t pos, linenoise_completions_t *lc, void *udata)
{
    const editor *ed = udata;
    token t = {BAR, 0, 0, 0, 0}; /* Start of line acts as a stage boundary. */
    scan(buf, pos, NULL, last, &t);
    size_t start = pos;
    if (t.end == pos && t.kind != BAR) {
        if (t.kind != WORD || !t.command || !t.plain) return;
        start = t.start;
    } else if (t.kind != BAR) {
        return; /* After an argument. */
    }
    lc->start = start;
    const char *prefix = buf + start;
    size_t n = pos - start;
    for (size_t i = 0; i < ed->count; ++i)
        if (!strncmp(ed->names[i], prefix, n))
            linenoise_add_completion(lc, ed->names[i]);
}

editor *editor_open(void)
{
    editor *ed = calloc(1, sizeof(*ed));
    if (!ed) return NULL;
    ed->ctx = linenoise_context_create();
    if (!ed->ctx) { free(ed); return NULL; }
    linenoise_set_multiline(ed->ctx, 1);
    linenoise_set_completion_callback(ed->ctx, complete, ed);
    linenoise_set_highlight_callback(ed->ctx, highlight, ed);
    linenoise_history_set_max_len(ed->ctx, 1000);
    const char *home = getenv("HOME");
    if (home && *home) {
        size_t size = strlen(home) + sizeof("/.cmdr_history");
        ed->history = malloc(size);
        if (ed->history) {
            snprintf(ed->history, size, "%s/.cmdr_history", home);
            linenoise_history_load(ed->ctx, ed->history); /* Absent on first run. */
        }
    }
    return ed;
}

char *editor_read(editor *ed, const char *prompt, const char *const *names,
                  size_t count, int *interrupted)
{
    ed->names = names;
    ed->count = count;
    char *text = linenoise_read(ed->ctx, prompt);
    ed->names = NULL;
    ed->count = 0;
    *interrupted = !text && linenoise_get_error() == LINENOISE_ERR_INTERRUPTED;
    if (!text) return NULL;
    char *line = strdup(text); /* linenoise_free, not free, releases text. */
    linenoise_free(text);
    return line;
}

void editor_remember(editor *ed, const char *pipeline)
{
    /* Newlines are whitespace to the parser, so one line recalls the pipeline. */
    char *flat = strdup(pipeline);
    if (!flat) return;
    for (char *p = flat; *p; ++p)
        if (*p == '\n' || *p == '\r') *p = ' ';
    size_t n = strlen(flat);
    while (n && flat[n - 1] == ' ') flat[--n] = '\0';
    if (n) {
        linenoise_history_add(ed->ctx, flat);
        if (ed->history && linenoise_history_save(ed->ctx, ed->history))
            fprintf(stderr, "cmdr: %s: cannot save history\n", ed->history);
    }
    free(flat);
}

void editor_close(editor *ed)
{
    if (!ed) return;
    linenoise_context_destroy(ed->ctx);
    free(ed->history);
    free(ed);
}
