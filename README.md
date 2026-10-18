# cmdr

A small C REPL for commands with positional arguments, text and list values, and explicit effects. Requires a C11 compiler and CMake 3.16+ on Linux or macOS, using POSIX-2008 (`getline`, `open_memstream`, `posix_spawn`, `fstatat`). Make is an optional frontend for the same CMake build.

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

The REPL edits lines with a vendored copy of linenoise, in [thirdparty/linenoise](thirdparty/linenoise). The prompt is light green, except on terminals without escape sequences (`dumb`, `cons25`, `emacs`). It colors commands, references, blocks, and strings, and shows unknown commands in red. Tab completes command names. Up and Down recall history, saved in `~/.cmdr_history`, with a continued pipeline stored as one entry. Ctrl-C drops the current pipeline. Pasted lines run one at a time, each echoed after its prompt. Configure with `-DCMDR_EDITOR=OFF` to read plain lines instead.

- `./cmdr -c 'PIPELINE'` evaluates one pipeline.

- `./cmdr FILE` runs a script and stops at its first failing line, reporting `FILE:LINE:`.

- Piped stdin runs one pipeline per line and continues after errors.

- `load PATH` runs a script in the current session, so its definitions remain.

- `./cmdr --user ...` runs `~/.cmdr/prelude.cmdr` instead of the standard prelude; it is an error if the file is missing.

- `./cmdr --version` prints the version.

The REPL starts with a line such as `cmdr 0.1.0 (prelude: standard)`, or the prelude's path with `--user`, in bold cyan where the prompt has color.

Errors go to stderr; the exit status is nonzero if any line failed. Lines starting with `#`, including a `#!/usr/bin/env cmdr` shebang, are comments.
A line ending in an unescaped `|` continues on the next line; blank and
comment lines inside a continuation are skipped. Quotes and blocks cannot span lines otherwise: write a newline as `\n`, and
end a line inside a block with `|` to continue it.

## Composition

`a | b x` evaluates `a`, then calls `b` with the returned value followed by
`x`. The value stays one argument, even when it contains spaces, newlines, or pipes. A command returns text, a list, or no value; no value cannot feed another command. The REPL prints text as is and list items one per line.

Whitespace separates arguments. Single and double quotes preserve spaces and pipes, including empty arguments (`""`). Backslash escapes the next character outside single quotes; inside double quotes, `\n` and `\t` are a newline and a tab. Syntax and command names are checked for the whole pipeline before any command runs. Effects from earlier stages remain if a later stage fails. There is no variable expansion, globbing, redirection, or implicit shell.

## Blocks and definitions

```text
cmdr> def wrap { (x d) text $d $x $d }
cmdr> list a b | map wrap - | join ,
- a -,- b -
cmdr> def clean-or { (s fallback) trim $s | try { nonempty | upper } $fallback }
cmdr> text '   ' | clean-or none
none
cmdr> def without { (xs v) keep $xs { not { equals $v } } }
cmdr> list a b c | without b | join ,
a,c
```

`{ ... }` is a block: a pipeline passed as a value. Its syntax, command names, and references are checked with the enclosing line, before any stage runs.

- `{ (a b) ... }` declares parameters, referenced as `$a` and `$b`. A block takes exactly the arguments it declares. `{ ... }` takes one input, received by pipe; `{ () ... }` takes none.

- Declared parameters appear only where named, and each must be used. Only `{ ... }` pipes its input into its first stage, so a block can start from
  something else: `{ (w) read app.log | contains $w }`.

- Each later stage receives the previous result first. A stage that is a lone `$name` makes that value the result.

- Names are lexical. A nested block sees the names of enclosing blocks, and an inner name shadows an outer one. A block keeps the values it captured.

- References substitute values, not text: a list stays one argument. Quote or escape `$` and braces for literal text; `a$x` is literal. Unquoted braces inside a block must balance.

`def NAME BLOCK [DOC]` defines a command for the rest of the session. `help` shows DOC, or the body if DOC is absent. Definitions are lexical: a body uses the commands that existed when it was defined, and a body that names its own command calls the one it shadows. A definition may shadow any command except `def`. Errors from a body are prefixed with its name (`outer: inner: equals: ...`). A mismatch from a def called on the line also shows the call's arguments: `ends-with ar bar: ge: 2 is not at least 3`.

`each`, `keep`, `not`, and `try` take blocks. Extra arguments follow the item:
`list a b c | keep { (s v) not $s { equals $v } } b`.

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
| `sort`, `sort-n`, `uniq`, `count` VALUE | Byte-order or numeric sort, adjacent dedup, item count |
| `head`, `tail` VALUE N; `filter VALUE TEXT` | Select items |
| `map VALUE COMMAND [ARG ...]` | Call a command per item |
| `each VALUE BLOCK [ARG ...]` | Call a block per item |
| `keep VALUE BLOCK [ARG ...]` | Keep items whose block holds |
| `not VALUE BLOCK [ARG ...]` | Return VALUE if the block mismatches |
| `with VALUE BLOCK [ARG ...]` | Call a block with VALUE |
| `fold VALUE INIT BLOCK [ARG ...]` | Combine items left to right; the block takes the result so far and the item |
| `try [INPUT] BLOCK FALLBACK` | Return FALLBACK if the block fails |
| `or [INPUT] BLOCK FALLBACK` | Return FALLBACK if the block mismatches |
| `check [INPUT] BLOCK MESSAGE` | Mismatch with MESSAGE if the block mismatches |
| `nonempty VALUE`, `equals TEXT X`, `contains TEXT X`, `matches TEXT REGEX`, `lt`/`gt`/`le`/`ge NUMBER X` | Predicates; REGEX is POSIX extended |
| `read PATH`, `write VALUE PATH`, `append VALUE PATH` | Files; write and append return VALUE |
| `files [DIR]`, `basename PATH`, `dirname PATH` | Regular files in DIR, sorted; lexical path parts |
| `run PROGRAM [ARG ...]`, `run-in TEXT PROGRAM [ARG ...]` | Run a program, returning stdout |
| `load PATH` | Run a script in this session |

```text
cmdr> text "pear,apple,pear" | split , | sort | uniq | join ", "
apple, pear
cmdr> add 2 3 | mul 4 | sub 1
19
cmdr> list a bb '' | keep { nonempty } | map upper | join ,
A,BB
cmdr> text "hello world" | run-in tr a-z A-Z
HELLO WORLD
```

List commands accept lists or newline-separated text; in text, a final newline ends the last item. `sort`, `uniq`, `head`, `tail`, `filter`, and `keep` return text for text input and lists for lists, so
`read app.log | filter ERROR | upper` works. `lines`, `split`, `map`, `each`,
`list`, and `files` return lists. Text commands reject lists; use `map` per item or `join` first. `write` and `append` store lists one item per line.

**Predicates** return their input when they hold, so they compose mid-pipeline. When they do not hold they fail with a mismatch, which `keep`, `not`, and `try` treat as false. Any other failure is an error, including one inside the block. Empty text and `"0"` are not false.

**`try`** returns the block's result, or FALLBACK if it fails at run time. Its arity is checked first, so a typo or a missing input is an error, not a fallback. `each { try { read } "" }` reads each file or substitutes empty text.

**`or`** is `try` for predicates: only a mismatch selects FALLBACK, and errors propagate. `with` passes a computed value to any argument position: `length $x | with { (n) drop $s $n }`. `check` replaces a mismatch's message, so a composed predicate can report `b is not a` instead of the commands it is built from.

**`run`** resolves PROGRAM through PATH and passes arguments unchanged. `run` supplies empty stdin; `run-in` supplies TEXT. Output is returned after the program exits. A nonzero exit status, such as `grep` without matches, fails the stage. The program's stderr is not captured; it goes to cmdr's stderr.

Text is bytes in the C locale. Files and program output containing NUL bytes are rejected.

## Growing a prelude

Start with a pipeline. This one lists the distinct file extensions in the current directory:

```text
cmdr> files | map basename | keep { drop 1 | contains . } | each { split . | tail 1 | join "" } | sort | uniq
md
txt
```

`keep { drop 1 | contains . }` keeps names with a dot after the first byte, so
it skips `Makefile` and `.gitignore`. `tail 1 | join ""` takes the last item.

[src/prelude.cmdr](src/prelude.cmdr) names the parts, each built on the ones before. It also defines list, number, text, and path commands, such as `nth`, `sum`, `max`, `starts-with`, and `stem`; `help` lists them. These are three of its definitions:

```
def last { nonempty | tail 1 | join "" } "Return the last item"
def ext { split . | last } "Return the text after the last dot"
def has-ext { keep { drop 1 | contains . } } "Keep names with a dot after the first byte"
```

A session builds on them:

```text
cmdr> def exts { (dir) files $dir | map basename | has-ext | map ext | sort | uniq } "List extensions in DIR"
cmdr> exts .
md
txt
```

Put definitions in a file, such as `ext.cmdr`, and run `load ext.cmdr` in a session or at the top of a script. Move a definition into `src/prelude.cmdr` when every session should have it. The build embeds the file, and cmdr runs it at startup; an error there stops cmdr with `prelude.cmdr:LINE:`. Because definitions are lexical, a later `def tail` does not change the prelude's `last`.

## Adding commands

Write a command in C only when it cannot be composed. Add it to the `commands` table in [src/main.c](src/main.c), where it shadows the primitives in [src/primitives.c](src/primitives.c). Set `.run` to a `cmdr_fn` from [src/cmdr.h](src/cmdr.h):

```c
static int initial(const cmdr *repl, size_t argc, const cmdr_value *argv,
                 cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: initial TEXT");
    *result = cmdr_text(argv[0].text, argv[0].text[0] ? 1 : 0);
    return 0;
}

/* In the commands table: */
{.name = "initial", .usage = "initial TEXT", .docstring = "Return the first byte",
 .run = initial, .text = 1},
```

`.text = 1` guarantees text arguments. Arguments are borrowed, including the values a block captured; clone what you keep. The result is owned by the caller. A predicate returns `CMDR_MISMATCH` when it does not hold; `cmdr_describe` gives a short form of a value for its message. Allocation failure exits the process.

## Checks

`make test` builds cmdr and runs [tests/test_cmdr.py](tests/test_cmdr.py) with Python 3, in parallel. Each test method is a CTest test, so `ctest --test-dir build -R test_editor` runs only the editor tests. Tests that need the editor show as skipped in a build without it. Configure with `-DCMDR_BUILD_TESTS=OFF` to build without Python.

Design history: [docs/dev/minimal.md](docs/dev/minimal.md), [docs/dev/blocks.md](docs/dev/blocks.md).
