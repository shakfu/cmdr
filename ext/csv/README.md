# csv

Read and write CSV, as Python's `csv` module with its default `excel` dialect.

| Command | Result |
|-|-|
| `csv.rows TEXT [DELIMITER]` | A list of rows, each a list of its fields as text. |
| `csv.dicts TEXT [DELIMITER]` | A list of dicts, one per row after the header, keyed by the header's fields. |
| `csv.format ROWS [DELIMITER]` | ROWS, a list of lists of text, as CSV text. |

```text
cmdr> import csv
cmdr> import dict
cmdr> read data.csv | csv.dicts | each { dict.get qty } | sum
5
cmdr> list $(list name qty) $(list 'apple, red' 3) | csv.format
name,qty
"apple, red",3
```

DELIMITER is one byte, `,` by default; `"\t"` reads tab-separated text.

Reading follows Python's `csv.reader`:

- A field in `"` may hold the delimiter, line breaks, and `""`, which is one `"`.
- A `"` inside an unquoted field is text.
- A row ends at `\r\n`, `\n`, or `\r`. A blank line is an empty row.
- A quote that is never closed, or text after a closing quote, as in `"a"b`, is an error. Python accepts the latter unless `strict=True`.

`csv.dicts` skips blank rows, as Python's `DictReader` does. A header that names a field twice, or a row with a different number of fields, is an error; `DictReader` fills short rows with `None` and keeps extra fields under `None`, and cmdr has no `None`.

`csv.format` quotes a field only when it holds the delimiter, `"`, or a line break, or when it is a row's only field and empty, as Python's `QUOTE_MINIMAL`. Each row ends with `\n`, where Python writes `\r\n`.

A whole file is one TEXT, since a quoted field may span lines; `fs.each-line` cannot split CSV.
