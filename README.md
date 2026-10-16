# cmdr

A small C REPL for commands with positional arguments, text and list values,
and explicit effects. Requires a C11 compiler and CMake 3.16+ on Linux or
macOS, using POSIX-2008 (`getline`, `open_memstream`, `posix_spawn`,
`fstatat`). Make is an optional frontend for the same CMake build.

```sh
make
./build/cmdr
```

```text
cmdr> text "hello world" | upper | write greeting.txt
HELLO WORLD
cmdr> read greeting.txt | lower
hello world
```

Run `help` for the command list; it returns text, so `help | filter map`
works. Exit with `quit`, `exit`, or Ctrl-D.

- `./cmdr -c 'PIPELINE'` evaluates one pipeline.
- `./cmdr FILE` runs a script and stops at its first failing line, reporting
  `FILE:LINE:`.
- Piped stdin runs one pipeline per line and continues after errors.

Errors go to stderr; the exit status is nonzero if any line failed. Lines
starting with `#`, including a `#!/usr/bin/env cmdr` shebang, are comments.
A line ending in an unescaped `|` continues on the next line; blank and
comment lines inside a continuation are skipped.

## Composition

`a | b x` evaluates `a`, then calls `b` with the returned value followed by
`x`. The value stays one argument, even when it contains spaces, newlines, or
pipes. A command returns text, a list, or no value; no value cannot feed
another command. The REPL prints text as is and list items one per line.

Whitespace separates arguments. Single and double quotes preserve spaces and
pipes, including empty arguments (`""`). Backslash escapes the next character
outside single quotes; inside double quotes, `\n` and `\t` are a newline and
a tab. Syntax and command names are checked for the whole pipeline before any
command runs. Effects from earlier stages remain if a later stage fails.
There is no variable expansion, globbing, redirection, or implicit shell.

## Parameters and definitions

```text
cmdr> def wrap 'text $2 $1 $2'
cmdr> list a b | map wrap - | join ,
- a -,- b -
cmdr> def clean-or 'trim | try "nonempty | upper" $2'
cmdr> text '   ' | clean-or none
none
cmdr> def stats2 '$2 | sort | uniq | count | text of $1'
cmdr> def stats 'count | stats2 $1'
cmdr> list b a b | stats
2 of 3
```

`def NAME PIPELINE` defines a command for the rest of the session.

- `$1..$N` are its arguments; `$1` is the piped value or the first explicit
  argument. A body whose highest reference is `$N` requires exactly N
  arguments; a body without references accepts at most one.
- The first stage receives `$1` as input unless it references `$1`.
- A stage that is a lone `$N` replaces the current value, so a body can start
  from any argument.
- References substitute values, not text: a list stays one argument. Quote or
  escape `$` for literal text; `a$1` is literal.
- Definitions are lexical: a body uses the commands that existed when it was
  defined, and a body that names its own command calls the one it shadows.
  A definition may shadow any command except `def`.
- Errors from a body are prefixed with its name (`outer: inner: equals: ...`).

Strings passed to `each`, `keep`, `not`, and `try` are pipelines with their
own `$1..$N`. Pass outer arguments explicitly:
`list a b c | keep 'not "equals $2" $2' b`.

## Commands

| Commands | Behavior |
| --- | --- |
| `text [ARG ...]` | Join arguments with spaces |
| `upper`, `lower`, `trim`, `length` TEXT | Case, whitespace, byte length |
| `replace TEXT OLD NEW` | Replace all literal matches; OLD is nonempty |
| `take TEXT N`, `drop TEXT N` | Keep or drop the first N bytes |
| `split TEXT SEPARATOR` | Split into a list; empty fields are kept |
| `add`, `mul` A B [NUMBER ...]; `sub`, `div` A B | Double-precision arithmetic |
| `list [VALUE ...]`, `lines VALUE` | Build a list; split text into lines |
| `join VALUE SEPARATOR` | Join text items |
| `sort`, `uniq`, `count` VALUE | Byte-order sort, adjacent dedup, item count |
| `head`, `tail` VALUE N; `filter VALUE TEXT` | Select items |
| `map VALUE COMMAND [ARG ...]` | Call a command per item |
| `each VALUE PIPELINE [ARG ...]` | Run a pipeline per item, item as `$1` |
| `keep VALUE PIPELINE [ARG ...]` | Keep items whose pipeline holds |
| `not VALUE PIPELINE [ARG ...]` | Return VALUE if the pipeline mismatches |
| `try [INPUT] PIPELINE FALLBACK` | Return FALLBACK if the pipeline fails |
| `nonempty VALUE`, `equals TEXT X`, `contains TEXT X`, `lt`/`gt NUMBER X` | Predicates |
| `read PATH`, `write VALUE PATH`, `append VALUE PATH` | Files; write and append return VALUE |
| `files [DIR]`, `basename PATH`, `dirname PATH` | Regular files in DIR, sorted; lexical path parts |
| `run PROGRAM [ARG ...]`, `run-in TEXT PROGRAM [ARG ...]` | Run a program, returning stdout |

```text
cmdr> text "pear,apple,pear" | split , | sort | uniq | join ", "
apple, pear
cmdr> add 2 3 | mul 4 | sub 1
19
cmdr> list a bb '' | keep nonempty | map upper | join ,
A,BB
cmdr> text "hello world" | run-in tr a-z A-Z
HELLO WORLD
```

List commands accept lists or newline-separated text; in text, a final
newline ends the last item. `sort`, `uniq`, `head`, `tail`, `filter`, and
`keep` return text for text input and lists for lists, so
`read app.log | filter ERROR | upper` works. `lines`, `split`, `map`, `each`,
`list`, and `files` return lists. Text commands reject lists; use `map` per
item or `join` first. `write` and `append` store lists one item per line.

**Predicates** return their input when they hold, so they compose
mid-pipeline. When they do not hold they fail with a mismatch, which `keep`,
`not`, and `try` treat as false. Any other failure is an error, including one
inside the pipeline. Empty text and `"0"` are not false.

**`try`** returns the pipeline's result, or FALLBACK if it fails at run time.
Its syntax, command names, and arity are checked first, so a typo is an
error, not a fallback. `map try read ""` reads each file or substitutes empty
text.

**`run`** resolves PROGRAM through PATH and passes arguments unchanged. `run`
supplies empty stdin; `run-in` supplies TEXT. Output is returned after the
program exits. A nonzero exit status, such as `grep` without matches, fails
the stage.

Text is bytes in the C locale. Files and program output containing NUL bytes
are rejected.

## Adding commands

Add entries to the `commands` table in [src/main.c](src/main.c); they shadow
the prelude in [src/prelude.c](src/prelude.c). Set `.pipeline` to compose
existing commands, or `.run` to a `cmdr_fn` from [src/cmdr.h](src/cmdr.h):

```c
static int first(const cmdr *repl, size_t argc, const cmdr_value *argv,
                 cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: first TEXT");
    *result = cmdr_text(argv[0].text, argv[0].text[0] ? 1 : 0);
    return 0;
}

/* In the commands table: */
{.name = "first", .usage = "first TEXT", .docstring = "Return the first byte",
 .run = first, .text = 1},
{.name = "shout", .usage = "shout TEXT", .docstring = "Trim and uppercase",
 .pipeline = "trim | upper"},
```

`.text = 1` guarantees text arguments. Arguments are borrowed; the result is
owned by the caller. A predicate returns `CMDR_MISMATCH` when it does not
hold. Allocation failure exits the process.

## Checks

`make test` builds cmdr and runs [tests/test_cmdr.py](tests/test_cmdr.py)
with Python 3. Configure with `-DCMDR_BUILD_TESTS=OFF` to build without
Python.

Design history: [docs/dev/minimal.md](docs/dev/minimal.md).
