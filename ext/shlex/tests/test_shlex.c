/* What test.cmdr cannot check, such as error messages. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_shlex;

int main(void)
{
    const cmdr table = {cmdr_ext_shlex.commands, cmdr_ext_shlex.count, &cmdr_primitives};
    expect(&table, "shlex-split \"a 'b c'\" | join ,", "a,b c");
    expect_error(&table, "shlex-split \"'open\"", 1, "shlex-split: no closing quotation");
    expect_error(&table, "shlex-split \"end\\\\\"", 1, "shlex-split: no escaped character");
    expect_error(&table, "shlex-split", 1, "usage: shlex-split TEXT");
    expect(&table, "text \"x\ny z\" | shlex-join", "x 'y z'");
    expect_error(&table, "shlex-join { upper }", 1, "usage: shlex-join LIST_OR_LINES");
    expect_error(&table, "list a | with { (x) list $x | shlex-join }", 1, "shlex-join: expected text items");
    puts("test_shlex: ok");
    return 0;
}
