/* Validity cases after JSONTestSuite's y_ and n_ files, and messages. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_json;

static const char *valid[] = {
    "[]", "{}", "\"\"", "0", "-0", "-0.0e+1", "1E5", "1e-5", "[[]]", " [1] ", "\"\\/\"",
    "{\"\":0}", "\"\\u0041\"", "\"\\ud800\"", "true", "null", "[1,\"a\",{\"b\":[null]}]",
    "\"\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\"",
};

static const char *invalid[] = {
    "", " ", "[1,]", "[,1]", "{\"a\"}", "{\"a\":}", "{a:1}", "{\"a\":1,}", "[01]", "[1.]", "[.1]",
    "[-]", "[1e]", "[+1]", "\"a\x01\"", "\"a\tb\"", "\"\\x\"", "\"\\u12\"", "\"\\u12g4\"", "[NaN]",
    "[Infinity]", "tru", "[1]]", "[1] x", "\"abc", "[\"a\"", "\"\xff\"", "\"\xc0\xaf\"",
    "\"\xed\xa0\x80\"", "\"\xf4\x90\x80\x80\"", "\"\xe2\x82\"", "[1 2]",
};

/* An array nested depth deep. */
static void nested(char *line, size_t depth)
{
    char *p = line + sprintf(line, "json.valid ");
    for (size_t i = 0; i < depth; ++i) *p++ = '[';
    for (size_t i = 0; i < depth; ++i) *p++ = ']';
    *p = '\0';
}

int main(void)
{
    const cmdr *table = ext_commands("json", &cmdr_ext_json);
    char line[2048];
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); ++i) {
        snprintf(line, sizeof(line), "json.valid '%s' | equals '%s'", valid[i], valid[i]);
        expect(table, line, valid[i]);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        snprintf(line, sizeof(line), "json.valid '%s'", invalid[i]);
        expect_error(table, line, CMDR_MISMATCH, "json.valid: invalid JSON at byte");
    }
    nested(line, 512);
    expect_error(table, strcat(line, " | not { json.valid }"), CMDR_MISMATCH, "");
    nested(line, 513);
    expect_error(table, line, CMDR_MISMATCH, "nested too deeply");

    expect_error(table, "json.get nope", 1, "json.get: invalid JSON at byte 0: expected a value");
    expect_error(table, "json.get '{\"a\": 1' a", 1, "json.get: invalid JSON at byte 7: expected ',' or '}'");
    expect_error(table, "json.get '{\"a\": 1}' a b", 1, "json.get: cannot index a number with b");
    expect_error(table, "json.get '[1]' x", 1, "json.get: array index is not an integer: x");
    expect_error(table, "json.get '{\"a\": 1}' z", CMDR_MISMATCH, "json.get: no key z");
    expect_error(table, "json.get '[1]' 3", CMDR_MISMATCH, "json.get: index 3 out of range for 1 items");
    expect_error(table, "json.get '[1]' -2", CMDR_MISMATCH, "out of range");
    expect_error(table, "json.text '[1]' 0", 1, "json.text: expected a string, got number");
    expect_error(table, "json.text '\"\\u0000\"'", 1, "json.text: text cannot hold NUL");
    expect_error(table, "json.text '\"\\ud800x\"'", 1, "json.text: a lone surrogate cannot be text");
    expect_error(table, "json.text '\"\\ude00\\ud83d\"'", 1, "a lone surrogate");
    expect_error(table, "json.keys '{\"\\udc00\": 1}'", 1, "json.keys: a lone surrogate cannot be text");
    expect_error(table, "json.keys '[1]'", 1, "json.keys: expected an object, got array");
    expect_error(table, "json.values 1", 1, "json.values: expected an object or array, got number");
    expect_error(table, "json.str '\xff'", 1, "json.str: TEXT is not UTF-8");
    expect_error(table, "json.object n x", 1, "json.object: invalid JSON at byte 0");
    expect_error(table, "json.object a", 1, "usage: json.object [KEY JSON ...]");
    expect_error(table, "json.strings a", 1, "usage: json.strings [KEY TEXT ...]");
    expect_error(table, "json.array $(list $(list 1))", 1, "json.array: expected JSON text");
    expect_error(table, "json.set '{}' 1", 1, "usage: json.set JSON SEG [SEG ...] VALUE");
    expect_error(table, "json.set 1 a 2", 1, "json.set: cannot index a number with a");
    expect_error(table, "json.set '{}' a x", 1, "json.set: invalid JSON");
    expect_error(table, "json.format 1 65", 1, "json.format: INDENT 65 is above 64");
    expect_error(table, "json.format 1 x", 1, "");

    /* Python escapes DEL and control characters; the raw bytes are UTF-8. */
    expect(table, "json.str \"\x7f\x01\"", "\"\\u007f\\u0001\"");
    expect(table, "json.format '\"\\b\\f\\n\\r\\t\\\"\\\\\"'", "\"\\b\\f\\n\\r\\t\\\"\\\\\"");
    expect(table, "json.format '\"\xf0\x9f\x98\x80\"'", "\"\\ud83d\\ude00\"");
    puts("test_json: ok");
    return 0;
}
