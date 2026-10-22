/* Multiline and flat layouts of cmdr source. The layout convention is in
 * docs/dev/multiline-blocks.md. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 100, INDENT = 4 };

typedef struct {
    char *text;
    size_t used, capacity;
} buffer;

static void put(buffer *b, const char *s, size_t n)
{
    if (b->used + n + 1 > b->capacity)
        b->text = cmdr_realloc(b->text, b->capacity = 2 * (b->used + n + 1), 1);
    memcpy(b->text + b->used, s, n);
    b->text[b->used += n] = '\0';
}

static void puts_(buffer *b, const char *s) { put(b, s, strlen(s)); }

static void spaces(buffer *b, size_t n)
{
    while (n--) put(b, " ", 1);
}

typedef struct {
    const char *text;
    size_t n;
} span;

/* A comment without trailing blanks. */
static span trim(const char *text, size_t n)
{
    while (n && isspace((unsigned char)text[n - 1])) --n;
    return (span){text, n};
}

typedef struct pipeline pipeline;

typedef struct {
    span word;      /* A word or reference, verbatim. */
    int sub;        /* A $( ... ) sub-pipeline. */
    char *params;   /* A block's "(a b=1)", or NULL. */
    pipeline *body; /* A block's or sub-pipeline's; NULL for a word. */
} item;

typedef struct {
    item *items;
    size_t count;
    span *comments; /* Lines before the stage. */
    size_t ncomments;
} stage;

struct pipeline {
    stage *stages;
    size_t count;
    span *comments; /* Lines after the last stage. */
    size_t ncomments;
};

static void push_span(span **spans, size_t *n, span s)
{
    *spans = cmdr_realloc(*spans, *n + 1, sizeof(**spans));
    (*spans)[(*n)++] = s;
}

static stage *add_stage(pipeline *p)
{
    p->stages = cmdr_realloc(p->stages, p->count + 1, sizeof(*p->stages));
    p->stages[p->count] = (stage){0};
    return &p->stages[p->count++];
}

static void free_pipeline(pipeline *p)
{
    for (size_t i = 0; i < p->count; ++i) {
        for (size_t j = 0; j < p->stages[i].count; ++j) {
            free(p->stages[i].items[j].params);
            if (p->stages[i].items[j].body) free_pipeline(p->stages[i].items[j].body);
        }
        free(p->stages[i].items);
        free(p->stages[i].comments);
    }
    free(p->stages);
    free(p->comments);
    free(p);
}

typedef struct {
    cmdr_token *tokens;
    size_t count;
} token_list;

static int collect(const cmdr_token *t, void *data)
{
    token_list *list = data;
    list->tokens = cmdr_realloc(list->tokens, list->count + 1, sizeof(*list->tokens));
    list->tokens[list->count++] = *t;
    return 0;
}

/* The pipeline in tokens t[*i, n), to a closing bracket or the end. A comment
 * belongs to the stage after it, or to the pipeline's end. */
static pipeline *build(const char *s, const cmdr_token *t, size_t n, size_t *i)
{
    pipeline *p = cmdr_realloc(NULL, 1, sizeof(*p));
    *p = (pipeline){0};
    stage *st = add_stage(p);
    span *pending = NULL;
    size_t npending = 0;
    while (*i < n && t[*i].kind != CMDR_TOKEN_BLOCK_CLOSE && t[*i].kind != CMDR_TOKEN_SUB_CLOSE) {
        const cmdr_token *k = &t[(*i)++];
        if (k->kind == CMDR_TOKEN_COMMENT) {
            push_span(&pending, &npending, trim(s + k->start, k->end - k->start));
            continue;
        }
        if (k->kind == CMDR_TOKEN_PIPE) {
            st = add_stage(p);
            continue;
        }
        if (!st->count) {
            st->comments = pending;
            st->ncomments = npending;
            pending = NULL;
            npending = 0;
        }
        item it = {0};
        if (k->kind == CMDR_TOKEN_BLOCK_OPEN || k->kind == CMDR_TOKEN_SUB_OPEN) {
            it.sub = k->kind == CMDR_TOKEN_SUB_OPEN;
            if (*i < n && t[*i].kind == CMDR_TOKEN_PARAMS_OPEN) {
                buffer params = {0};
                puts_(&params, "(");
                for (++*i; *i < n && t[*i].kind != CMDR_TOKEN_PARAMS_CLOSE; ++*i) {
                    /* A default is a word that starts at its '='. */
                    if (t[*i].kind == CMDR_TOKEN_PARAM && params.used > 1) puts_(&params, " ");
                    put(&params, s + t[*i].start, t[*i].end - t[*i].start);
                }
                ++*i;
                puts_(&params, ")");
                it.params = params.text;
            }
            it.body = build(s, t, n, i);
            ++*i;
        } else {
            it.word = (span){s + k->start, k->end - k->start};
        }
        st->items = cmdr_realloc(st->items, st->count + 1, sizeof(*st->items));
        st->items[st->count++] = it;
    }
    p->comments = pending;
    p->ncomments = npending;
    return p;
}

static int empty(const pipeline *p) { return p->count == 1 && !p->stages[0].count; }

static int has_comments(const pipeline *p)
{
    if (p->ncomments) return 1;
    for (size_t i = 0; i < p->count; ++i) {
        if (p->stages[i].ncomments) return 1;
        for (size_t j = 0; j < p->stages[i].count; ++j)
            if (p->stages[i].items[j].body && has_comments(p->stages[i].items[j].body)) return 1;
    }
    return 0;
}

static void flat(buffer *b, const pipeline *p);

static void opener(buffer *b, const item *it)
{
    if (it->sub) {
        puts_(b, "$(");
    } else {
        puts_(b, "{");
        if (it->params) { puts_(b, " "); puts_(b, it->params); }
    }
}

static void flat_item(buffer *b, const item *it)
{
    if (!it->body) {
        put(b, it->word.text, it->word.n);
    } else if (it->sub) {
        opener(b, it);
        flat(b, it->body);
        puts_(b, ")");
    } else {
        opener(b, it);
        if (!empty(it->body)) { puts_(b, " "); flat(b, it->body); }
        puts_(b, " }");
    }
}

static void flat(buffer *b, const pipeline *p)
{
    for (size_t i = 0; i < p->count; ++i) {
        if (i) puts_(b, " | ");
        for (size_t j = 0; j < p->stages[i].count; ++j) {
            if (j) puts_(b, " ");
            flat_item(b, &p->stages[i].items[j]);
        }
    }
}

static size_t flat_width(const pipeline *p)
{
    buffer b = {0};
    flat(&b, p);
    size_t n = b.used;
    free(b.text);
    return n;
}

static size_t item_width(const item *it)
{
    buffer b = {0};
    flat_item(&b, it);
    size_t n = b.used;
    free(b.text);
    return n;
}

/* Whether a stage's first line fits from column: its items up to the first
 * broken one, and that one's opener. */
static int fits(const stage *s, const char *broken, size_t column)
{
    size_t width = column;
    for (size_t j = 0; j < s->count; ++j) {
        if (j) ++width;
        if (!broken[j]) { width += item_width(&s->items[j]); continue; }
        buffer b = {0};
        opener(&b, &s->items[j]);
        width += b.used;
        free(b.text);
        break;
    }
    return width <= WIDTH;
}

static void body_lines(buffer *b, const pipeline *p, size_t indent);

/* Append stage s to a line already column wide, whose stages start at
 * indent. Blocks and sub-pipelines break, last first, until the line fits;
 * one holding a comment always breaks. */
static void stage_lines(buffer *b, const stage *s, size_t indent, size_t column)
{
    char *broken = cmdr_realloc(NULL, s->count + 1, 1);
    for (size_t j = 0; j < s->count; ++j)
        broken[j] = s->items[j].body && has_comments(s->items[j].body);
    while (!fits(s, broken, column)) {
        size_t first = 0, j;
        while (first < s->count && !broken[first]) ++first;
        for (j = first; j && !(s->items[j - 1].body && !broken[j - 1]); --j) continue;
        if (!j) break;
        broken[j - 1] = 1;
    }
    for (size_t j = 0; j < s->count; ++j) {
        const item *it = &s->items[j];
        if (j) puts_(b, " ");
        if (!broken[j]) { flat_item(b, it); continue; }
        opener(b, it);
        puts_(b, "\n");
        body_lines(b, it->body, indent + INDENT);
        spaces(b, indent);
        puts_(b, it->sub ? ")" : "}");
    }
    free(broken);
}

static void comment_lines(buffer *b, const span *c, size_t n, size_t indent)
{
    for (size_t i = 0; i < n; ++i) {
        spaces(b, indent);
        put(b, c[i].text, c[i].n);
        puts_(b, "\n");
    }
}

/* A broken body: one stage per line, later ones led by '|'. */
static void body_lines(buffer *b, const pipeline *p, size_t indent)
{
    for (size_t i = 0; i < p->count && !empty(p); ++i) {
        comment_lines(b, p->stages[i].comments, p->stages[i].ncomments, indent);
        spaces(b, indent);
        if (i) puts_(b, "| ");
        stage_lines(b, &p->stages[i], indent, indent + (i ? 2 : 0));
        puts_(b, "\n");
    }
    comment_lines(b, p->comments, p->ncomments, indent);
}

/* Every comment in p, in order, at column 0: flat moves them above it. */
static void hoist(buffer *b, const pipeline *p)
{
    for (size_t i = 0; i < p->count; ++i) {
        comment_lines(b, p->stages[i].comments, p->stages[i].ncomments, 0);
        for (size_t j = 0; j < p->stages[i].count; ++j)
            if (p->stages[i].items[j].body) hoist(b, p->stages[i].items[j].body);
    }
    comment_lines(b, p->comments, p->ncomments, 0);
}

/* Width of the buffer's last line. */
static size_t column(const buffer *b)
{
    size_t i = b->used;
    while (i && b->text[i - 1] != '\n') --i;
    return b->used - i;
}

static int lines_fit(const char *text)
{
    for (size_t n; *text; text += n + (text[n] == '\n'))
        if ((n = strcspn(text, "\n")) > WIDTH) return 0;
    return 1;
}

/* A top-level pipeline. One that does not fit breaks its blocks, keeping its
 * stages on one line. If that still overflows, or comments sit between its
 * stages, each stage takes a line, continued by a trailing '|'. */
static void top(buffer *b, const pipeline *p, int flat_layout)
{
    if (flat_layout || (!has_comments(p) && flat_width(p) <= WIDTH)) {
        if (flat_layout) hoist(b, p);
        flat(b, p);
        puts_(b, "\n");
        return;
    }
    int between = 0;
    for (size_t i = 0; i < p->count; ++i) between |= p->stages[i].ncomments != 0;
    if (!between) {
        buffer one = {0};
        puts_(&one, "");
        for (size_t i = 0; i < p->count; ++i) {
            if (i) puts_(&one, " | ");
            stage_lines(&one, &p->stages[i], 0, column(&one));
        }
        int fit = p->count == 1 || lines_fit(one.text);
        if (fit) {
            puts_(b, one.text);
            puts_(b, "\n");
        }
        free(one.text);
        if (fit) return;
    }
    for (size_t i = 0; i < p->count; ++i) {
        size_t indent = i ? INDENT : 0;
        comment_lines(b, p->stages[i].comments, p->stages[i].ncomments, indent);
        spaces(b, indent);
        stage_lines(b, &p->stages[i], indent, indent);
        puts_(b, i + 1 < p->count ? " |\n" : "\n");
    }
}

typedef enum { BLANK, COMMENT, PIPELINE } line_kind;

typedef struct {
    line_kind kind;
    size_t line;       /* Its first line, from 1. */
    size_t start, end; /* Bytes of the source, newlines included. */
} chunk;

/* Split source into blank lines, comment lines, and pipelines, as a script
 * runs them, and check each pipeline's syntax. */
static int split(const char *source, chunk **chunks, size_t *count, char **error)
{
    char *joined = NULL, *text = NULL;
    size_t used = 0, line = 0, first = 0, first_line = 0;
    int status = 0;
    *chunks = NULL;
    *count = 0;
    for (const char *p = source; *p && !status;) {
        size_t n = strcspn(p, "\n");
        n += p[n] == '\n';
        ++line;
        if (!used) {
            first = (size_t)(p - source);
            first_line = line;
        }
        p += n;
        if (cmdr_join(&joined, &used, p - n, n) && *p) continue;
        used = 0;
        chunk c = {PIPELINE, first_line, first, (size_t)(p - source)};
        const char *q = source + c.start;
        while (q < p && isspace((unsigned char)*q)) ++q;
        if (q == p) c.kind = BLANK;
        else if (*q == '#' && line == first_line) c.kind = COMMENT;
        if (c.kind == PIPELINE) {
            text = cmdr_strndup(source + c.start, c.end - c.start);
            char *inner;
            if (cmdr_check_syntax(text, &inner)) {
                status = cmdr_error(error, "%zu: %s", c.line, inner);
                free(inner);
            }
            free(text);
        }
        *chunks = cmdr_realloc(*chunks, *count + 1, sizeof(**chunks));
        (*chunks)[(*count)++] = c;
    }
    free(joined);
    if (status) free(*chunks);
    return status;
}

static pipeline *tree(const char *source, const chunk *c)
{
    token_list list = {0};
    cmdr_scan(source + c->start, c->end - c->start, NULL, collect, &list);
    size_t i = 0;
    pipeline *p = build(source + c->start, list.tokens, list.count, &i);
    free(list.tokens);
    return p;
}

/* What formatting must keep: each pipeline's tokens, and every comment in
 * order. */
static int signature(const char *source, buffer *code, buffer *comments, char **error)
{
    chunk *chunks;
    size_t count;
    if (split(source, &chunks, &count, error)) return 1;
    for (size_t i = 0; i < count; ++i) {
        token_list list = {0};
        const char *s = source + chunks[i].start;
        if (chunks[i].kind != BLANK)
            cmdr_scan(s, chunks[i].end - chunks[i].start, NULL, collect, &list);
        if (chunks[i].kind == PIPELINE) puts_(code, "\n");
        for (size_t j = 0; j < list.count; ++j) {
            const cmdr_token *t = &list.tokens[j];
            int comment = t->kind == CMDR_TOKEN_COMMENT;
            buffer *to = comment ? comments : code;
            span text = {s + t->start, t->end - t->start};
            if (comment) text = trim(text.text, text.n);
            char kind = (char)('A' + t->kind);
            put(to, &kind, 1);
            put(to, text.text, text.n);
            put(to, "", 1);
        }
        free(list.tokens);
    }
    free(chunks);
    return 0;
}

int cmdr_format(const char *source, int flat_layout, char **out, char **error)
{
    *out = NULL;
    *error = NULL;
    chunk *chunks;
    size_t count;
    if (split(source, &chunks, &count, error)) return 1;
    buffer b = {0};
    puts_(&b, "");
    for (size_t i = 0; i < count; ++i) {
        const chunk *c = &chunks[i];
        if (c->kind == BLANK) {
            puts_(&b, "\n");
        } else if (c->kind == COMMENT) {
            const char *q = source + c->start;
            while (isspace((unsigned char)*q)) ++q;
            span text = trim(q, (size_t)(source + c->end - q));
            put(&b, text.text, text.n);
            puts_(&b, "\n");
        } else {
            pipeline *p = tree(source, c);
            top(&b, p, flat_layout);
            free_pipeline(p);
        }
    }
    free(chunks);
    /* Refuse output that would change the program. */
    buffer before[2] = {{0}, {0}}, after[2] = {{0}, {0}};
    char *inner = NULL;
    int status = signature(source, &before[0], &before[1], error)
                 || signature(b.text, &after[0], &after[1], &inner);
    for (int k = 0; !status && k < 2; ++k)
        if (before[k].used != after[k].used
            || (before[k].used && memcmp(before[k].text, after[k].text, before[k].used)))
            status = 1;
    if (status && !*error)
        cmdr_error(error, "formatting would change the program%s%s; please report this",
                   inner ? ": " : "", inner ? inner : "");
    free(inner);
    for (int k = 0; k < 2; ++k) { free(before[k].text); free(after[k].text); }
    if (status) free(b.text);
    else *out = b.text;
    return status;
}
