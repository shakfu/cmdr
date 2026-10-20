#define _POSIX_C_SOURCE 200809L
#include "editor.h"

#include "linenoise.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* linenoise colors: 1 red, 2 green, 3 yellow, 4 blue, 5 magenta, 6 cyan;
 * adding 8 makes them bold. A binding is a named value, so it is a bold
 * reference color. */
enum { UNKNOWN = 1, STRING = 2, PUNCTUATION = 3, COMMAND = 4, REFERENCE = 5, COMMENT = 6,
       BINDING = 13 };

struct editor {
    linenoise_context_t *ctx;
    const cmdr *repl;
    char *history; /* NULL when $HOME is unset. */
    char *saving;  /* history with ".tmp" appended. */
    int color;     /* The terminal handles escape sequences. */
    char *pasted;  /* A submitted buffer with several lines, from linenoise. */
    char *next;    /* Its next line to return. */
};

/* The active command that buf[0, n) names, or NULL. */
static const cmdr_command *known(const cmdr *repl, const char *name, size_t n)
{
    char word[256];
    if (n >= sizeof(word)) return NULL;
    memcpy(word, name, n);
    word[n] = '\0';
    return cmdr_find(repl, word);
}

typedef struct {
    const editor *ed;
    const char *buf;
    char *colors;
    size_t pos;
    int piped;   /* The current stage receives a value, so NAME is as's first argument. */
    int binding; /* The current stage is the built-in as. */
    int args;    /* Arguments seen in the current stage. */
} paint;

static int paint_token(const cmdr_token *t, void *data)
{
    paint *p = data;
    size_t n = t->end - t->start;
    char *at = p->colors + t->start;
    cmdr_token_kind kind = t->kind;
    if (kind == CMDR_TOKEN_COMMENT) memset(at, COMMENT, n);
    else if (kind == CMDR_TOKEN_REFERENCE || kind == CMDR_TOKEN_PARAM) memset(at, REFERENCE, n);
    else if (kind != CMDR_TOKEN_WORD) memset(at, PUNCTUATION, n);
    /* A stage after '|', or first in a block without parameters, is piped. */
    if (kind == CMDR_TOKEN_PIPE || kind == CMDR_TOKEN_BLOCK_OPEN) p->piped = 1, p->binding = 0;
    else if (kind == CMDR_TOKEN_SUB_OPEN) p->piped = 0, p->binding = 0; /* It receives no input. */
    else if (kind == CMDR_TOKEN_SUB_CLOSE || kind == CMDR_TOKEN_BLOCK_CLOSE) p->binding = 0;
    else if (kind == CMDR_TOKEN_PARAMS_OPEN) p->piped = 0;
    if (kind != CMDR_TOKEN_WORD && kind != CMDR_TOKEN_REFERENCE) return 0;
    if (!t->command) {
        /* as's NAME: as VALUE NAME, or NAME alone after a piped VALUE. */
        if (p->binding && ++p->args == (p->piped ? 1 : 2) && kind == CMDR_TOKEN_WORD && t->plain)
            memset(at, BINDING, n);
        return 0;
    }
    p->binding = 0, p->args = 0;
    if (kind != CMDR_TOKEN_WORD || !t->plain) return 0;
    const cmdr_command *command = known(p->ed->repl, p->buf + t->start, n);
    if (command) {
        memset(at, command->value ? BINDING : COMMAND, n);
        /* The built-in, not a def that shadows it. */
        p->binding = n == 2 && !memcmp(p->buf + t->start, "as", 2) && !command->block;
    } else if (p->pos != t->end && !(n == 4 && !memcmp(p->buf + t->start, "[...", 4))) {
        /* A word still being typed is not marked unknown, nor the start of
         * linenoise's "[... N pasted lines ...]", which buf shows for a paste. */
        memset(at, UNKNOWN, n);
    }
    return 0;
}

static void highlight(const char *buf, char *colors, size_t len, size_t pos, void *udata)
{
    paint p = {.ed = udata, .buf = buf, .colors = colors, .pos = pos};
    unsigned char *quoted = malloc(len ? len : 1); /* Without it, strings stay plain. */
    cmdr_scan(buf, len, quoted, paint_token, &p);
    for (size_t i = 0; quoted && i < len; ++i)
        if (quoted[i]) colors[i] = STRING;
    free(quoted);
}

static int last(const cmdr_token *t, void *data)
{
    *(cmdr_token *)data = *t;
    return 0;
}

/* Complete command names where a command may start: at the line start, after
 * '|', '{', or "$(", or after a block's parameter list. */
static void complete(const char *buf, size_t pos, linenoise_completions_t *lc, void *udata)
{
    const editor *ed = udata;
    cmdr_token t = {CMDR_TOKEN_PIPE, 0, 0, 0, 0}; /* The line start acts as a stage boundary. */
    cmdr_scan(buf, pos, NULL, last, &t);
    int boundary = t.kind == CMDR_TOKEN_PIPE || t.kind == CMDR_TOKEN_BLOCK_OPEN
                   || t.kind == CMDR_TOKEN_SUB_OPEN || t.kind == CMDR_TOKEN_PARAMS_CLOSE;
    size_t start = pos;
    if (t.end == pos && !boundary) {
        if (t.kind != CMDR_TOKEN_WORD || !t.command || !t.plain) return;
        start = t.start;
    } else if (!boundary) {
        return; /* After an argument. */
    }
    lc->start = start;
    const char *prefix = buf + start;
    size_t n = pos - start;
    for (const cmdr *group = ed->repl; group; group = group->parent)
        for (size_t i = 0; i < group->count; ++i) {
            const cmdr_command *command = &group->commands[i];
            if (cmdr_find(ed->repl, command->name) != command) continue;
            if (!strncmp(command->name, prefix, n))
                linenoise_add_completion(lc, command->name);
        }
}

/* linenoise prints the prompt as is on these terminals, so escape sequences
 * would show as text. The list is linenoise's unsupported_term. */
static int plain_terminal(void)
{
    static const char *plain[] = {"dumb", "cons25", "emacs"};
    const char *term = getenv("TERM");
    for (size_t i = 0; term && i < sizeof(plain) / sizeof(*plain); ++i)
        if (!strcasecmp(term, plain[i])) return 1;
    return 0;
}

const char *editor_prompt(const editor *ed, int continued)
{
    if (continued) return "...> ";
    return ed->color ? "\x1b[92mcmdr>\x1b[0m " : "cmdr> ";
}

int editor_color(const editor *ed)
{
    return ed->color;
}

editor *editor_open(const cmdr *repl)
{
    editor *ed = calloc(1, sizeof(*ed));
    if (!ed) return NULL;
    ed->ctx = linenoise_context_create();
    if (!ed->ctx) { free(ed); return NULL; }
    ed->repl = repl;
    ed->color = !plain_terminal();
    linenoise_set_multiline(ed->ctx, 1);
    linenoise_set_completion_callback(ed->ctx, complete, ed);
    linenoise_set_highlight_callback(ed->ctx, highlight, ed);
    linenoise_history_set_max_len(ed->ctx, 1000);
    const char *home = getenv("HOME");
    if (home && *home) {
        size_t size = strlen(home) + sizeof("/.cmdr_history.tmp");
        ed->history = malloc(size);
        ed->saving = malloc(size);
        if (ed->history && ed->saving) {
            snprintf(ed->history, size, "%s/.cmdr_history", home);
            snprintf(ed->saving, size, "%s.tmp", ed->history);
            linenoise_history_load(ed->ctx, ed->history); /* Absent on first run. */
        } else {
            free(ed->history);
            free(ed->saving);
            ed->history = ed->saving = NULL;
        }
    }
    return ed;
}

/* A pasted buffer keeps its newlines. Return it one line at a time, echoed
 * after its prompt, so each line runs as typed instead of as one pipeline. */
ssize_t editor_read(editor *ed, const char *prompt, char **line, size_t *capacity)
{
    const char *text = ed->next;
    if (!text) {
        char *read = linenoise_read(ed->ctx, prompt);
        if (!read) return linenoise_get_error() == LINENOISE_ERR_INTERRUPTED ? -2 : -1;
        if (strchr(read, '\n')) ed->pasted = ed->next = read;
        text = read;
    }
    size_t n = strcspn(text, "\n");
    if (n && text[n - 1] == '\r') --n;
    if (n + 2 > *capacity) {
        *line = cmdr_realloc(*line, n + 2, 1);
        *capacity = n + 2;
    }
    memcpy(*line, text, n);
    (*line)[n] = '\n';
    (*line)[n + 1] = '\0';
    if (ed->pasted) {
        printf("%s%.*s\n", prompt, (int)n, *line);
        fflush(stdout);
        const char *rest = strchr(text, '\n');
        ed->next = rest && rest[1] ? (char *)rest + 1 : NULL; /* Skip a final newline. */
        if (!ed->next) { linenoise_free(ed->pasted); ed->pasted = NULL; }
    } else {
        linenoise_free((char *)text);
    }
    return (ssize_t)n + 1;
}

void editor_remember(editor *ed, const char *pipeline)
{
    /* Newlines are whitespace to the parser, so one line recalls the pipeline. */
    char *flat = cmdr_strndup(pipeline, strlen(pipeline));
    for (char *p = flat; *p; ++p)
        if (*p == '\n' || *p == '\r') *p = ' ';
    size_t n = strlen(flat);
    while (n && flat[n - 1] == ' ') flat[--n] = '\0';
    if (n) {
        linenoise_history_add(ed->ctx, flat);
        /* linenoise truncates before writing, so write a copy and rename it:
         * a failed save keeps the previous history. */
        if (ed->history && (linenoise_history_save(ed->ctx, ed->saving)
                            || rename(ed->saving, ed->history)))
            fprintf(stderr, "cmdr: %s: cannot save history\n", ed->history);
    }
    free(flat);
}

void editor_close(editor *ed)
{
    if (!ed) return;
    linenoise_context_destroy(ed->ctx);
    linenoise_free(ed->pasted);
    free(ed->history);
    free(ed->saving);
    free(ed);
}
