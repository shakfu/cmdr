/* Invalid input, which test.cmdr cannot write in ASCII source. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_utf8;

int main(void)
{
    const cmdr *table = ext_commands("utf8", &cmdr_ext_utf8);
    static const char *bad[] = {"\xff", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\x80"};
    char line[64];
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        snprintf(line, sizeof(line), "utf8.valid 'ab%s'", bad[i]);
        expect_error(table, line, CMDR_MISMATCH, "utf8.valid: invalid UTF-8 at byte 2");
        snprintf(line, sizeof(line), "utf8.length 'ab%s'", bad[i]);
        expect_error(table, line, 1, "utf8.length: invalid UTF-8 at byte 2");
    }
    expect_error(table, "utf8.take '\xff' 1", 1, "utf8.take: invalid UTF-8 at byte 0");
    expect_error(table, "utf8.drop '\xff' 1", 1, "utf8.drop: invalid UTF-8");
    expect_error(table, "utf8.chars '\xff'", 1, "utf8.chars: invalid UTF-8");
    expect_error(table, "utf8.take abc x", 1, "");
    expect(table, "utf8.length '\xf0\x9f\x98\x80\xc3\xa9'", "2");
    expect(table, "utf8.take '\xf0\x9f\x98\x80\xc3\xa9' 1", "\xf0\x9f\x98\x80");
    expect(table, "utf8.chars '\xe2\x82\xac\xc3\xa9" "a'", "\xe2\x82\xac\n\xc3\xa9\na\n");
    puts("test_utf8: ok");
    return 0;
}
