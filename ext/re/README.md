# re

Extract, split, and replace with POSIX extended regular expressions, matched bytewise as `matches` does.

| Command | Result |
|-|-|
| `re-find TEXT REGEX` | The first match. A mismatch if there is none. |
| `re-groups TEXT REGEX` | A list: the first match, then each group. An unmatched group is empty text. A mismatch if there is no match. |
| `re-all TEXT REGEX` | A list of all nonempty matches, left to right. |
| `re-split TEXT REGEX` | TEXT split at nonempty matches. Empty text is an empty list, as with `split`. |
| `re-replace TEXT REGEX REPLACEMENT` | TEXT with every match replaced. `\0` to `\9` insert the match and its groups; `\\` inserts a backslash. |

```text
cmdr> re-replace 2024-01-31 '([0-9]+)-([0-9]+)-([0-9]+)' '\3/\2/\1'
31/01/2024
cmdr> re-groups key=value '([a-z]+)=(.*)' | tail 2
key
value
```

Quote REGEX and REPLACEMENT in single quotes, which keep backslashes.

Empty matches follow sed's `s///g`: `re-replace abc 'x*' -` is `-a-b-c-`. `re-all` and `re-split` skip them, so `x*` cannot split between every byte.

`matches` tests for a match but cannot return one, so extraction needs C.
