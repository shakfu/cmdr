# Multi-line blocks

Decision record, 2026-10-22, against `2e7feb7`. Implemented as proposed, with the decisions under [Decisions](#decisions) and the formatter under [Formatter](#formatter).

## Background

A pipeline is one physical line. A line ending in `|` continues on the next, and blank and comment lines between are skipped (`cmdr_join` in `src/session.c`). A block cannot otherwise span lines. Its first stage must start on the line that opens it.

The three input modes disagree:

| Mode | Splitter | A newline inside `{ }` |
|-|-|-|
| Script, `load` | `run_script`: `getline`, then `cmdr_join` | Error: `unclosed '{'`, unless the line ends in `\|` |
| REPL | `cmdr_join` per line read | Same as scripts |
| `-c` | `line_length` in `src/main.c`: first newline outside quotes, blocks, and `$(...)` | Whitespace |

In `-c`, `parse` treats the newline as a space. A body written as two lines runs as one stage:

```text
$ cmdr -c 'list a | each { text x
upper }'
a x upper
```

CHANGELOG 0.2.0 kept this for quoted newlines ("A newline inside quotes or a block stays in its pipeline, as before"). The block case is the misparse that the REPL paste fix removed for top-level lines.

### Evidence

The repository's 1163 lines of `.cmdr` hold 60 lines over 200 characters. The longest is 686. Nearly all are `def`s with nested blocks, such as `tracked`, `edit-parse`, and `edit-stage` in `examples/agent-workflows/aw/__init__.cmdr`:

```text
def tracked { (d) git-in $d $(list ls-files) | lines | with { (fs) list dir $d | run-with find . -path ./.git -prune -o ! -type f ! -type d -print | agent-out | lines | each { strip-prefix ./ } | with { (bad) keep $fs { (f) edit-prefixes $f | keep { (x) any $bad { equals $x } } | empty } } } } "Tracked files in DIR that are regular files on disk, not through a link"
```

## Proposal

1. All three modes join lines while a `{` or `$(` is open, as `-c` does now. `line_length` goes, and `-c` uses `cmdr_join`.
2. Inside `{ }` or `$( )`, a newline is whitespace only in these positions:
   - after `{` or the parameter list;
   - before or after `|`;
   - before `}` or `)`.

   Anywhere else it is an error: `newline inside a block must be next to '|'`.
3. Inside a block, a line whose first non-blank character is `#` is a comment. Blank lines are skipped. This extends the README's rule, "A line starting with `#` is a comment", to block bodies.
4. At top level, only a trailing `|` continues a line. A leading `|` would need the REPL to read ahead.

`tracked` becomes:

```text
def tracked { (d)
    git-in $d $(list ls-files)
    | lines
    | with { (fs)
        list dir $d
        | run-with find . -path ./.git -prune -o ! -type f ! -type d -print
        | agent-out
        | lines
        | each { strip-prefix ./ }
        | with { (bad)
            keep $fs { (f)
                edit-prefixes $f
                | keep { (x) any $bad { equals $x } }
                | empty
            }
        }
    }
} "Tracked files in DIR that are regular files on disk, not through a link"
```

The docstring stays on the line of the last `}`. At top level, a newline after `}` ends the pipeline.

### Why newline-next-to-`|`

Rule 2 makes a missing `|` an error, so a body cannot misparse. A newline gains no meaning of its own: removing every newline gives the same pipeline. The alternatives:

| Meaning of a newline in a block | `{ (x)` / `text $x` / `upper }` | Rejected because |
|-|-|-|
| Whitespace (current `-c`) | `text $x upper` | Silent misparse. Shell habit produces exactly this layout. |
| Stage separator | `text $x \| upper` | A newline means `\|` inside a block but "new pipeline" outside. Arguments cannot wrap. |
| Statement separator | Two pipelines; the block returns the last | Adds sequencing to blocks: order of effects, and scope of `as` inside a body. A semantic change, not syntax. |
| Indentation-significant (offside rule) | Depends on columns | Braces already delimit blocks. Two delimiting systems must then agree. |

### Layout convention

Indentation has no meaning to the parser. A `|` belongs to the innermost open block. A layout can therefore show a stage in the wrong block:

```text
            | with { (bad) keep $fs { (f) edit-prefixes $f
                | keep { (x) any $bad { equals $x } }
                | empty }
            }
```

Here `| keep` and `| empty` belong to `(f)`, not `(bad)`. Moving one `}` changes which, and the layout does not show it. The README would state a convention:

- A block whose body spans lines ends its opening line with `{` or `{ (params)`.
- The body is indented one level.
- Each `|` aligns with the first stage of its block.
- `}` stands on its own line, aligned with the line that opened the block.
- Short blocks stay inline: `{ strip-prefix ./ }`.

`cmdr --fmt` produces this layout; see [Formatter](#formatter).

## Failure modes

- An unclosed `{` joins every later line. The error names the pipeline's first line, `FILE:12: unclosed '{'`, so its location stays correct.
- Errors name a pipeline's first line. Inside a 20-line `def`, `FILE:12:` is less precise. A line offset in compile errors would fix this; it is not part of this proposal.
- Arguments cannot wrap: `list a b c ...` stays on one line. Add a continuation only when a real case needs it.

## Changes

| File | Change |
|-|-|
| `src/session.c` | `cmdr_join` continues while a block or sub-pipeline is open |
| `src/cmdr.c` | `parse` enforces rule 2 and skips comment lines after an allowed newline |
| `src/main.c` | `line_length` and its splitter go; `-c` joins as scripts do |
| `editors/tree-sitter-cmdr/grammar.js` | Newlines allowed inside `block` and `sub_pipeline` per rule 2; corpus tests |
| `editors/vim`, `editors/sublime`, `editors/vscode` | None: their block regions span lines, and comments match `^\s*#` |
| `README.md`, `docs/grammar.ebnf` | "Words and lines" section; the grammar's joining and `body` rules |

The REPL already prints `...> ` while a pipeline continues.

## Decisions

1. **Trailing `|` inside blocks:** allowed, as is leading. One rule for blocks and the top level, where a trailing `|` continues a line. The formatter emits leading `|` inside blocks.
2. **Quotes spanning lines:** not allowed, in any mode. A quote open at a line's end stops joining, so the error is `unclosed quote` on that line, not a block that takes the rest of the file. `cmdr -c $'text "a\nb"'` worked in 0.2.0 and is now an error; write `\n`.
3. **`$(...)` bodies:** same rule as blocks.

## Formatter

`cmdr --fmt [--flat] [--check | -w] [FILE ...]`, over `cmdr_format` in `src/format.c`. A flag, not a `fmt` subcommand: `cmdr fmt` already runs a script named `fmt`.

- **Splitting.** Pipelines end where `cmdr_join` ends them, so the formatter and the interpreter agree on every pipeline. Each is checked with `cmdr_check_syntax`, which compiles without resolving names. A file with a syntax error is refused, since formatting it could turn an error into a running program.
- **Multiline.** A pipeline that fits in 100 columns stays on one line. Otherwise its blocks and sub-pipelines break, last first, until the line up to the first broken one fits; a block holding a comment always breaks. A top-level pipeline whose lines still overflow, or with comments between its stages, puts each stage on a line after a trailing `|`. Words are never wrapped, so a long quoted string still overflows.
- **Flat.** Each pipeline on one line. Comments inside it move above it, in order; flat has no other place for them.
- **Blank lines** inside a pipeline are dropped. Top-level blank lines are kept.
- **Safety.** Before returning, `cmdr_format` re-reads its output and requires the same pipelines with the same tokens, and the same comments in the same order. Otherwise it fails and returns nothing.
