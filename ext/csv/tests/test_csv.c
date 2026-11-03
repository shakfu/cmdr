/* Messages, which test.cmdr cannot check. */
#define _POSIX_C_SOURCE 200809L
#include "cmdr.h"
#include "ext_check.h"

extern const cmdr cmdr_ext_csv;

int main(void)
{
    const cmdr *table = ext_commands("csv", &cmdr_ext_csv);
    expect_error(table, "csv.rows \"a\nb,\\\"c\"", 1, "csv.rows: row 2: unclosed quote");
    expect_error(table, "csv.rows '\"a\"b'", 1, "csv.rows: row 1: text after a closing quote");
    expect_error(table, "csv.rows a ab", 1, "csv.rows: DELIMITER must be one byte other than a quote or line break");
    expect_error(table, "csv.rows a '\"'", 1, "csv.rows: DELIMITER must be one byte");
    expect_error(table, "csv.dicts \"a,a\n1,2\"", 1, "csv.dicts: the header names a twice");
    expect_error(table, "csv.dicts \"a,b\n1\"", 1, "csv.dicts: row 2: 1 of the header's 2 fields");
    expect_error(table, "csv.format a", 1, "csv.format: ROWS must be a list of lists");
    expect_error(table, "list a | csv.format", 1, "csv.format: row 1 is not a list");
    expect_error(table, "list $(list a $(list b)) | csv.format", 1, "csv.format: row 1, field 2 is not text");
    expect_error(table, "csv.rows", 1, "usage: csv.rows TEXT [DELIMITER]");
    puts("test_csv: ok");
    return 0;
}
