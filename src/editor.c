#define _POSIX_C_SOURCE 200809L
#include "editor.h"

#include "linenoise.h"

#include <ctype.h>
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

typedef enum { WORD, REF, BAR, NOTE, OPEN, CLOSE, PARAMS, PARAM, PARAMS_END, SUB_OPEN,
               SUB_CLOSE } token_kind;

typedef struct {
    token_kind kind;
    size_t start, end;
    int command; /* First word of a stage. */
    int plain;   /* No quotes or escapes, so the text is the word. */
} token;

/* Split buf[0, len) as the lexer in cmdr.c does, tolerating an unfinished
 * line, calling visit per token and marking quoted bytes in colors when
 * non-NULL. Inside a block, unquoted braces nest anywhere, as in lex_block;
 * "(a b)" directly after '{' declares parameters. "$(" opens a sub-pipeline,
 * whose first word is a command, and the ')' that balances it closes it. */
static void scan(const char *buf, size_t len, char *colors,
                 void (*visit)(const token *, void *), void *data)
{
    size_t i = 0, depth = 0;
    while (i < len && isspace((unsigned char)buf[i])) ++i;
    if (i < len && buf[i] == '#') {
        visit(&(token){NOTE, i, len, 0, 0}, data);
        return;
    }
    int command = 1, opened = 0; /* opened: after '{', where "(a b)" may start. */
    /* Open sub-pipelines, each with the unquoted '(' its words opened. */
    size_t parens[64], subs = 0;
    while (i < len) {
        if (isspace((unsigned char)buf[i])) { ++i; continue; }
        token t = {WORD, i, i + 1, command, 1};
        char c = buf[i];
        if (c == '$' && i + 1 < len && buf[i + 1] == '(' && subs < 64) {
            t.kind = SUB_OPEN;
            t.end = i += 2;
            parens[subs++] = 0;
            command = 1;
            opened = 0;
            visit(&t, data);
            continue;
        }
        if (c == ')' && subs && !parens[subs - 1]) {
            t.kind = SUB_CLOSE;
            --subs;
            ++i;
            command = 0;
            visit(&t, data);
            continue;
        }
        if (c == '|' || c == '{' || (c == '}' && depth)) {
            t.kind = c == '|' ? BAR : c == '{' ? OPEN : CLOSE;
            depth = c == '{' ? depth + 1 : c == '}' ? depth - 1 : depth;
            command = c != '}';
            opened = c == '{';
            ++i;
            visit(&t, data);
            continue;
        }
        if (c == '(' && opened) {
            t.kind = PARAMS;
            visit(&t, data);
            for (++i; i < len && buf[i] != ')'; ) {
                if (isspace((unsigned char)buf[i])) { ++i; continue; }
                token p = {PARAM, i, i, 0, 1};
                while (i < len && !isspace((unsigned char)buf[i]) && buf[i] != ')' && buf[i] != '=')
                    ++i;
                p.end = i;
                visit(&p, data);
                if (i < len && buf[i] == '=') { /* A default: an ordinary word. */
                    token v = {WORD, i, i, 0, 1};
                    while (i < len && !isspace((unsigned char)buf[i]) && buf[i] != ')') ++i;
                    v.end = i;
                    visit(&v, data);
                }
            }
            if (i < len) visit(&(token){PARAMS_END, i, i + 1, 0, 0}, data), ++i;
            opened = 0;
            continue;
        }
        opened = 0;
        if (c == '$') {
            t.kind = REF;
            while (i < len && !isspace((unsigned char)buf[i]) && buf[i] != '|'
                   && !(depth && buf[i] == '}') && !(subs && buf[i] == ')')) ++i;
            t.end = i;
            command = 0;
            visit(&t, data);
            continue;
        }
        char quote = 0;
        size_t nested = 0; /* Braces opened inside this word, within a block. */
        while (i < len && (quote || (!isspace((unsigned char)buf[i]) && buf[i] != '|'))) {
            c = buf[i];
            size_t width = 1;
            if (!quote && depth && c == '}') {
                if (!nested) break; /* Closes the block. */
                --nested;
            }
            if (!quote && depth && c == '{') ++nested;
            if (!quote && subs && c == '(') ++parens[subs - 1];
            if (!quote && subs && c == ')') {
                if (!parens[subs - 1]) break; /* Closes the sub-pipeline. */
                --parens[subs - 1];
            }
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

static void paint_token(const token *t, void *data)
{
    paint *p = data;
    size_t n = t->end - t->start;
    char *at = p->colors + t->start;
    if (t->kind == NOTE) memset(at, COMMENT, n);
    else if (t->kind == REF || t->kind == PARAM) memset(at, REFERENCE, n);
    else if (t->kind != WORD) memset(at, PUNCTUATION, n);
    /* A stage after '|', or first in a block without parameters, is piped. */
    if (t->kind == BAR || t->kind == OPEN) p->piped = 1, p->binding = 0;
    else if (t->kind == SUB_OPEN) p->piped = 0, p->binding = 0; /* It receives no input. */
    else if (t->kind == SUB_CLOSE) p->binding = 0;
    else if (t->kind == PARAMS) p->piped = 0;
    else if (t->kind == CLOSE) p->binding = 0;
    if (t->kind != WORD && t->kind != REF) return;
    if (!t->command) {
        /* as's NAME: as VALUE NAME, or NAME alone after a piped VALUE. */
        if (p->binding && ++p->args == (p->piped ? 1 : 2) && t->kind == WORD && t->plain)
            memset(at, BINDING, n);
        return;
    }
    p->binding = 0, p->args = 0;
    if (t->kind != WORD || !t->plain) return;
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
}

static void highlight(const char *buf, char *colors, size_t len, size_t pos, void *udata)
{
    paint p = {.ed = udata, .buf = buf, .colors = colors, .pos = pos};
    scan(buf, len, colors, paint_token, &p);
}

static void last(const token *t, void *data)
{
    *(token *)data = *t;
}

/* Complete command names where a command may start: at the line start, after
 * '|' or '{', or after a block's parameter list. */
static void complete(const char *buf, size_t pos, linenoise_completions_t *lc, void *udata)
{
    const editor *ed = udata;
    token t = {BAR, 0, 0, 0, 0}; /* The line start acts as a stage boundary. */
    scan(buf, pos, NULL, last, &t);
    int boundary = t.kind == BAR || t.kind == OPEN || t.kind == PARAMS_END;
    size_t start = pos;
    if (t.end == pos && !boundary) {
        if (t.kind != WORD || !t.command || !t.plain) return;
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
