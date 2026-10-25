# str

Search, repeat, pad, split, and reverse text. Like `take` and `drop`, these count bytes, so they split multibyte UTF-8 characters; [utf8](../utf8/README.md) counts code points.

| Command | Result |
|-|-|
| `str.index TEXT SUBSTRING` | The byte offset of the first SUBSTRING, from 0. A mismatch if absent. |
| `str.repeat TEXT N` | TEXT repeated N times. |
| `str.pad-left TEXT WIDTH [FILL]` | TEXT with FILL, one byte (default space), added on the left up to WIDTH bytes. |
| `str.pad-right TEXT WIDTH [FILL]` | The same, on the right. |
| `str.chars TEXT` | A list of the bytes of TEXT. |
| `str.reverse LIST_OR_TEXT` | The items of a list, or the bytes of text, in reverse order. |

```text
cmdr> import str
cmdr> import num
cmdr> num.range 3 | each { str.pad-left 3 0 }
001
002
003
cmdr> files . | str.reverse | head 1
```

A result over 64 MiB is an error.

`str.reverse` takes text as bytes, unlike `sort` and `head`, which split text into lines. Use `lines | str.reverse` to reverse lines.

`str.repeat` and `str.chars` need a loop of N steps, which a `def` cannot express. `str.reverse` needs to build a list in a new order, and no primitive appends to a list.
