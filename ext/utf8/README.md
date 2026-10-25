# utf8

Count, cut, and split text by code points, as Python's `str` does. `length`, `take`, `drop`, and `str` count bytes, so they can split a multibyte character.

| Command | Result |
|-|-|
| `utf8.length TEXT` | The number of code points. |
| `utf8.take TEXT N` | The first N code points. |
| `utf8.drop TEXT N` | TEXT without its first N code points. |
| `utf8.chars TEXT` | A list of the code points, each as text. |
| `utf8.valid TEXT` | TEXT if it is valid UTF-8. A mismatch if not. |

```text
cmdr> import utf8
cmdr> import json
cmdr> json.text '"caf\u00e9"' | as word
cmdr> length $word
5
cmdr> utf8.length $word
4
cmdr> utf8.take $word 3
caf
```

Invalid UTF-8 is an error, with the byte offset of the first bad sequence; `utf8.valid` tests for it. Overlong forms, encoded surrogates, and code points above U+10FFFF are invalid, as RFC 3629 requires.

A code point is not a character as a reader sees one: `e` followed by U+0301 is two code points and one glyph, as in Python. Display width is not counted.
