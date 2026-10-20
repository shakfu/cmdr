# shlex

Split and quote shell words, as Python's `shlex` does with `posix=True`. Nothing is expanded or run.

| Command | Result |
|-|-|
| `shlex.split TEXT` | The words of TEXT, as a list. An unclosed quote or a trailing `\` is an error. |
| `shlex.quote TEXT` | TEXT quoted so a shell reads it as one word. Text of only `[A-Za-z0-9_@%+=:,./-]` is unchanged. |
| `shlex.join LIST_OR_LINES` | The items quoted and joined with spaces. `shlex.split` returns the items. Text is split into lines, as `join` does, never into words. |

```text
cmdr> import shlex
cmdr> shlex.split "git commit -m 'two words'" | count
4
cmdr> list a "b c" | shlex.join
a 'b c'
```

Whitespace separates words. `'...'` is literal. Outside quotes, `\` escapes any byte. Inside `"..."`, `\` escapes only `"` and `\`, as in Python; a POSIX shell also escapes `$`, `` ` ``, and newline.

`split` cuts at every separator and cannot honour quotes, so splitting needs a loop over bytes.
