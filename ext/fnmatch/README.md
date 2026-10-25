# fnmatch

Match names against shell-style patterns, with fnmatch(3).

| Command | Result |
|-|-|
| `fnmatch.match NAME PATTERN` | NAME if it matches PATTERN. A mismatch if not. |

```text
cmdr> import fnmatch
cmdr> import fs
cmdr> fs.walk src | keep { fnmatch.match '*.c' }
src/cmdr.c
src/editor.c
```

`*` matches any bytes, `?` one byte, and `[abc]`, `[a-z]`, and `[!abc]` one byte of a set. `*` and `?` match `/` and a leading `.`, so `*.c` matches `src/main.c` and `.c`. `\` escapes the next byte. A malformed pattern is an error.

cmdr itself never globs: a pattern is text until `fnmatch.match` reads it, and it lists nothing. `fs.walk` or `files` lists the names.

Python's `fnmatch.fnmatchcase` differs in two ways: `\` is literal there, and `[^abc]` matches `^` or a set member, where glibc reads it as `[!abc]`. macOS's fnmatch, from FreeBSD, likely does the same; this is untested. Quote PATTERN in single quotes, so cmdr keeps its backslashes.
