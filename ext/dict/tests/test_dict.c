/* Messages and printing, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_dict;

/* Render value of source after cmdr_renderable passes, or return its error. */
static void expect_printed(const cmdr *repl, const char *source, const char *printed)
{
    cmdr_value value;
    char *error = NULL, *text = NULL;
    size_t size = 0;
    CHECK(!cmdr_eval(repl, source, &value, &error));
    CHECK(!cmdr_renderable(&value, &error));
    FILE *out = open_memstream(&text, &size);
    CHECK(out && !cmdr_print(&value, out));
    fclose(out);
    if (strcmp(text, printed)) fprintf(stderr, "%s: got '%s', want '%s'\n", source, text, printed);
    CHECK(!strcmp(text, printed));
    cmdr_free(&value);
    free(text);
}

static void expect_unprintable(const cmdr *repl, const char *source, const char *message)
{
    cmdr_value value;
    char *error = NULL;
    CHECK(!cmdr_eval(repl, source, &value, &error));
    CHECK(cmdr_renderable(&value, &error) && strstr(error, message));
    cmdr_free(&value);
    free(error);
}

int main(void)
{
    const cmdr *table = ext_commands("dict", &cmdr_ext_dict);
    expect_printed(table, "dict.of name cmdr stars 3", "name\tcmdr\nstars\t3\n");
    expect_printed(table, "dict.of", "\n");
    expect_unprintable(table, "dict.of tags $(list a b)", "cannot print dict: value of tags is [a, b]");
    expect_unprintable(table, "list $(dict.of a $(dict.of b 1))", "value of a is dict {b: 1}");
    expect_unprintable(table, "dict.of a \"x\ty\"", "value of a holds a tab or newline");
    expect_unprintable(table, "dict.of \"a\nb\" x", "key holds a tab or newline");
    expect_error(table, "dict.get $(list a) a", 1, "dict.get: expected a dict, got [a]");
    expect_error(table, "dict.of $(list k) v", 1, "dict.of: KEY must be text, got [k]");
    expect_error(table, "dict.of a", 1, "usage: dict.of [KEY VALUE ...]");
    expect_error(table, "dict.get $(dict.of a 1) b", CMDR_MISMATCH, "dict.get: no key b");
    expect_error(table, "dict.from-items $(list $(list a 1) b)", 1, "item 2 is not a KEY VALUE pair");
    /* Commands that take text or lists reject a dict, and do not read it as a list. */
    expect_error(table, "count $(dict.of a 1)", 1, "count: expected text, got a dict");
    expect_error(table, "upper $(dict.of a 1)", 1, "command 'upper' expects text, got a dict");
    expect_error(table, "concat $(dict.of a 1)", 1, "concat: argument 1 is a dict");
    expect_error(table, "each $(dict.of a 1) { upper }", 1, "each: expected text, got a dict");
    puts("test_dict: ok");
    return 0;
}
