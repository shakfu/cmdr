/* Print each byte's token class, as cmdr_scan reads a script, one line per
 * source line: # comment, c command, w word, r reference, | pipe, { block
 * bracket, $ sub-pipeline bracket, ( parameter bracket, p parameter, and a
 * space for whitespace. test_syntax_corpus compares the editor grammars with
 * this. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *classes;
    size_t base;
} marks;

static int mark(const cmdr_token *t, void *data)
{
    marks *m = data;
    char c = 'w';
    switch (t->kind) {
    case CMDR_TOKEN_WORD: c = t->command ? 'c' : 'w'; break;
    case CMDR_TOKEN_REFERENCE: c = 'r'; break;
    case CMDR_TOKEN_PIPE: c = '|'; break;
    case CMDR_TOKEN_COMMENT: c = '#'; break;
    case CMDR_TOKEN_BLOCK_OPEN: case CMDR_TOKEN_BLOCK_CLOSE: c = '{'; break;
    case CMDR_TOKEN_SUB_OPEN: case CMDR_TOKEN_SUB_CLOSE: c = '$'; break;
    case CMDR_TOKEN_PARAMS_OPEN: case CMDR_TOKEN_PARAMS_CLOSE: c = '('; break;
    case CMDR_TOKEN_PARAM: c = 'p'; break;
    default: break;
    }
    memset(m->classes + m->base + t->start, c, t->end - t->start);
    return 0;
}

int main(int argc, char **argv)
{
    FILE *in = argc == 2 ? fopen(argv[1], "rb") : NULL;
    if (!in) {
        fprintf(stderr, "usage: scan FILE\n");
        return 2;
    }
    char *source = NULL;
    size_t size = 0, capacity = 0, n;
    char chunk[4096];
    while ((n = fread(chunk, 1, sizeof(chunk), in))) {
        if (size + n + 1 > capacity) source = realloc(source, capacity = (size + n + 1) * 2);
        memcpy(source + size, chunk, n);
        size += n;
    }
    fclose(in);
    if (!source) source = calloc(1, 1);
    source[size] = '\0';
    marks m = {calloc(size + 1, 1), 0};
    memset(m.classes, ' ', size);
    /* Scan each pipeline as a script joins it, so a command after a newline
     * at top level starts a new pipeline. */
    char *joined = NULL;
    size_t used = 0, first = 0;
    for (size_t at = 0; at < size;) {
        size_t len = strcspn(source + at, "\n");
        len += source[at + len] == '\n';
        if (!used) first = at;
        at += len;
        if (cmdr_join(&joined, &used, source + at - len, len) && at < size) continue;
        used = 0;
        m.base = first;
        cmdr_scan(source + first, at - first, NULL, mark, &m);
    }
    for (size_t i = 0; i < size; ++i) {
        if (source[i] == '\n') { putchar('\n'); continue; }
        putchar(source[i] == ' ' || source[i] == '\t' ? ' ' : m.classes[i]);
    }
    if (size && source[size - 1] != '\n') putchar('\n');
    free(joined);
    free(m.classes);
    free(source);
    return 0;
}
