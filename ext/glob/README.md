# glob

List the paths that match a shell-style pattern, as Python's `glob.glob`, with glob(3).

| Command | Result |
|-|-|
| `glob.glob PATTERN` | The paths that match PATTERN, sorted bytewise. An empty list if none match. |

```text
cmdr> import glob
cmdr> glob.glob 'src/*.c'
src/cmdr.c
src/editor.c
```

- `*` matches any bytes within one path component, `?` one byte, and `[abc]`, `[a-z]`, and `[!abc]` one byte of a set. None of them matches `/`.
- A component that starts with `.` matches only a pattern component that starts with `.`, so `*` skips dotfiles.
- `\` escapes the next byte. Quote PATTERN in single quotes, so cmdr keeps it.
- A pattern without wildcards gives the path if it exists.
- A pattern that ends in `/` matches directories, and each result keeps the `/`.
- `**` anywhere in PATTERN is an error. It is reserved for recursive matching; until then, `fs.walk` with `fnmatch.match` lists a tree.
- Directories that cannot be read are skipped.

Paths keep PATTERN's form: a relative pattern gives relative paths. Results are sorted, where Python returns them in directory order. As in Python, there is no `{a,b}` or `~` expansion.
