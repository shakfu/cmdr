/* Messages, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_lists;

int main(void)
{
    const cmdr *table = ext_commands("lists", &cmdr_ext_lists);
    expect_error(table, "list a | lists.set-at 2 x", 1, "lists.set-at: index 2 out of range for 1 items");
    expect_error(table, "list a | lists.set-at 0 x", 1, "out of range");
    expect_error(table, "list a | lists.index-of b", CMDR_MISMATCH, "lists.index-of: b is not an item");
    expect_error(table, "list a b | lists.sort-by { (x) equals $x a }", 1, "lists.sort-by item 2: ");
    expect_error(table, "list a | lists.sort-by { (x) list $x }", 1, "lists.sort-by item 1: the key must be text");
    expect_error(table, "list a | lists.sort-n-by { (x) $x }", 1, "invalid finite number: a");
    expect_error(table, "list a | lists.sort-by { (x y) concat $x $y }", 1, "lists.sort-by: ");
    expect_error(table, "list a | lists.sort-by x", 1, "lists.sort-by: expected a block { ... }");
    expect_error(table, "list a b | lists.group-by { (x) $x } extra", 1, "lists.group-by: ");
    expect_error(table, "list a b | lists.accumulate { (_ x) equals $x c }", 1, "lists.accumulate item 2: ");
    expect_error(table, "list a b | lists.accumulate { (x) $x }", 1, "lists.accumulate: ");
    expect_error(table, "list 1 | lists.batched 0", 1, "lists.batched: N must be at least 1");
    expect_error(table, "list 1 | lists.batched x", 1, "");
    expect_error(table, "list 1 | lists.enumerate -1", 1, "");
    expect_error(table, "lists.zip", 1, "usage: lists.zip LIST_OR_LINES [LIST_OR_LINES ...]");
    expect_error(table, "lists.flatten { text x }", 1, "lists.flatten: expected a list or lines");
    expect_error(table, "lists.zip $(list a) { text x }", 1, "lists.zip: expected a list or lines");
    expect_error(table, "list a | lists.index-of $(list a)", 1, "usage: lists.index-of LIST_OR_LINES TEXT");
    /* A key block that returns nothing. */
    expect_error(table, "list a | lists.sort-by { (_) import _lists }", 1, "lists.sort-by item 1: block returned no value");
    puts("test_lists: ok");
    return 0;
}
