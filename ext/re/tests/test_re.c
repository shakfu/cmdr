/* Error messages and statuses, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_re;

int main(void)
{
    const cmdr *table = ext_commands("re", &cmdr_ext_re);
    expect_error(table, "re.find abc '[0-9]'", CMDR_MISMATCH, "re.find: abc does not match '[0-9]'");
    expect_error(table, "re.groups abc x", CMDR_MISMATCH, "re.groups: abc does not match x");
    expect_error(table, "re.find abc '('", 1, "re.find: invalid regex '('");
    expect_error(table, "re.all abc '['", 1, "re.all: invalid regex '['");
    expect_error(table, "re.replace abc '(a)' '\\2'", 1, "re.replace: \\2 refers past 1 group");
    expect_error(table, "re.replace abc a '\\x'", 1, "use \\0 to \\9 or \\\\");
    expect_error(table, "re.replace abc a '\\'", 1, "use \\0 to \\9 or \\\\");
    expect_error(table, "re.find abc", 1, "usage: re.find TEXT REGEX");
    /* With ten groups, \9 is the last a replacement can name. */
    expect(table, "re.replace abcdefghij '(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)' '\\9\\1'", "ia");
    expect(table, "re.all aXbXc X | join ,", "X,X");
    puts("test_re: ok");
    return 0;
}
