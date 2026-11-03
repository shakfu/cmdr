/* The parser: cmdr_scan, cmdr_check_syntax, cmdr_format, and compiling a line
 * against an empty table, so no command runs. Properties: tokens lie inside
 * the input in order, and formatting is idempotent. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "fuzz.h"

typedef struct {
    size_t n, last;
} bounds;

static int visit(const cmdr_token *t, void *data)
{
    bounds *b = data;
    REQUIRE(t->start <= t->end && t->end <= b->n && t->start >= b->last);
    b->last = t->start;
    return 0;
}

static void format(const char *text, int flat)
{
    char *out = NULL, *again = NULL, *error = NULL;
    if (cmdr_format(text, flat, &out, &error)) {
        REQUIRE(error && !out);
        free(error);
        return;
    }
    REQUIRE(out && !error);
    int status = cmdr_format(out, flat, &again, &error);
    if (status || strcmp(out, again))
        fprintf(stderr, "format %s: '%s' then %s '%s'\n", flat ? "flat" : "multiline", out,
                status ? "fails:" : "gives", status ? error : again);
    REQUIRE(!status && !strcmp(out, again));
    free(out);
    free(again);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const cmdr empty = {NULL, 0, NULL};
    char *text = fuzz_text(data, size);
    size_t n = strlen(text);
    unsigned char *quoted = cmdr_realloc(NULL, n + 1, 1);
    bounds b = {n, 0};
    cmdr_scan(text, n, quoted, visit, &b);
    free(quoted);
    char *error = NULL;
    if (cmdr_check_syntax(text, &error)) REQUIRE(error);
    free(error);
    error = NULL;
    cmdr_value value;
    int status = cmdr_eval(&empty, text, &value, &error);
    REQUIRE(!status == !error);
    if (!status) cmdr_free(&value);
    free(error);
    format(text, 0);
    format(text, 1);
    free(text);
    return 0;
}
