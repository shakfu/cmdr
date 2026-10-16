# cmdr

A small REPL for commands with positional arguments, text and list values,
and explicit effects. The language is [src/cmdr.janet](src/cmdr.janet), written
in [Janet](https://janet-lang.org); [src/main.c](src/main.c) embeds it with the
Janet VM. Requires a C11 compiler and CMake 3.16+ on Linux/macOS. Janet 1.42.1
and [linenoise](https://github.com/shakfu/linenoise) are vendored in
`thirdparty/` (see each `VERSION` file). Set `-DCMDR_JANET_DIR=PATH` to link
another Janet release or build tree instead. Make is an optional frontend for
the same CMake build; pass CMake options as `make CMAKE_FLAGS=...`.
The interactive REPL uses linenoise unless configured with
`-DCMDR_LINENOISE=OFF`, which reads plain lines. It adds:

- Line editing, with Tab completing command names at the start of a stage.
- History in `~/.cmdr_history` (mode 0600), one line per pipeline.
- Highlighting of commands, unknown commands, quotes, `$` references, `|`,
  and comments.

Pipes and files always read plain lines.

```sh
make
./build/cmdr
```

```sh
cmake -S . -B build
cmake --build build
./build/cmdr
```

```text
cmdr> text "hello world" | upper
HELLO WORLD
cmdr> text "hello world" | upper | write greeting.txt
HELLO WORLD
cmdr> read greeting.txt | upper
HELLO WORLD
```

Run `help` for the command list; it returns text, so `help | filter map` works.
Exit with `quit`, `exit`, or Ctrl-D; Ctrl-C discards the current line or continuation. For scripts, pass a pipeline
with `./cmdr -c 'text hello | upper'`, run a file with `./cmdr FILE`, or feed
one pipeline per line on stdin. Errors go to stderr; an uncaught failure stops
its pipeline. The REPL continues after errors and exits with a nonzero status
if any line failed. A script file stops at its first failing line and reports
`FILE:LINE:`, since later lines may depend on earlier ones. `#` lines,
including a `#!/usr/bin/env cmdr` shebang, are comments. In files and on
stdin, a line ending in an unescaped `|` continues on the next line; blank and
comment lines inside a continuation are skipped, and errors report the first
line.

```text
def report 'read |
  lines |
  # Distinct lines only
  sort | uniq'
```

### Definitions

```text
cmdr> def clean-or 'trim | try "nonempty | upper" | recover $2'
cmdr> text '   ' | clean-or none
none
cmdr> def wrap 'text $2 $1 $2'
cmdr> list a b | map wrap - | join ,
- a -,- b -
```

`def NAME PIPELINE` registers a pipeline command for the rest of the session,
with the parameter rules below. The body's syntax and
command names are checked when it is defined, so define helpers before the
commands that use them. Arity is checked per call. A definition may shadow any
command except `def`, including earlier definitions; `help` marks
definitions with `def:`. Definitions are lexically scoped: a body uses the
commands that existed when it was defined. Redefining `upper` later does not
change earlier bodies, and a body that names its own command calls the
command it shadows:

```text
cmdr> def upper 'upper | text !'
cmdr> text a | upper
A !
```

Errors from a pipeline command's body are prefixed with its name, so nested
definitions report a call chain (`outer: inner: equals: ...`).

## Composition

Each command receives positional arguments and returns text, an explicit list,
or no value. `a | b x` evaluates `a`, then calls `b` with the returned value followed
by `x`. The value stays a single argument, even when it contains spaces, newlines,
or pipes. Lists retain their item boundaries throughout a pipeline. The REPL
prints only the final returned value: text as-is, and lists as newline-separated
items. It adds a final newline when needed. An empty string and an empty list
are values; no value cannot feed another command.

Commands run in order in the same process. `read` and `write` perform filesystem
effects; `write VALUE PATH` replaces the destination and returns VALUE so that
composition can continue. Earlier effects remain if a later stage fails. Syntax
and command names are checked for the entire pipeline before any command runs;
individual commands validate their arguments when called.

Whitespace separates arguments. Single and double quotes preserve spaces and
literal pipes, including empty arguments (`""`). Backslash escapes the next
character outside single quotes; inside double quotes, `\n` and `\t` are a
newline and a tab. Adjacent quoted/unquoted fragments form one
argument. A pipeline whose first nonblank character is `#` is a comment and
returns no value; `#` elsewhere is literal. Unquoted words starting with `$`
are parameter references (see below); quote or escape `$` for literal text.
`a$1` is literal. There is no variable expansion, implicit shell execution, redirection, or
background execution. Text values are not binary buffers: `read` and `run`
reject NUL bytes.

## Parameters

`def` bodies, and the pipeline strings that `try`, `each`, `keep`, `not`, and
`when` take, have parameters `$1..$N`. `$1` is the first argument, piped or
explicit. A body whose highest reference is `$N` requires exactly N arguments;
a body without references accepts at most one. The first stage receives `$1`
as input unless it references `$1` itself. References substitute typed values,
not text: a list stays one argument, and a value containing `|` is not parsed.

```text
cmdr> def wrap 'text $2 $1 $2'
cmdr> wrap x '*'
* x *
cmdr> list a b | map wrap - | join ,
- a -,- b -
cmdr> text x | try 'text pre $1'
pre x
```

A stage that is a lone reference replaces the current value with that value,
so a body can start from any parameter: `$2 | sort | uniq | count | text of $1`.
A captured failure is not replaced.

The current value is named while it is `$1` (at the start) or a lone
reference. A stage that references that name receives the value only where
named; any other stage receives it as its first argument. So `text $2` as a
first stage still receives `$1`; write `$2 | text` instead.

A nested string has its own `$1..$N`; pass outer values as arguments. A
reference without an argument, including `$1` at the top level, is an error.
There are no named bindings. To reuse an intermediate value, pass it to a
`def` as a parameter:

```text
cmdr> def stats2 '$2 | sort | uniq | count | text of $1'
cmdr> def stats 'count | stats2 $1'
cmdr> list b a b | stats
2 of 3
```

See [docs/dev/compose-ng.md](docs/dev/compose-ng.md) for the design and
[docs/dev/janet-kernel.md](docs/dev/janet-kernel.md) for the Janet
implementation.

## Adding commands

`register` in [src/cmdr.janet](src/cmdr.janet) adds an application command.
Application commands override prelude commands with the same name; `help`
lists each active name once.

```janet
(register "first" "first TEXT" "Return the first byte"
          (fn [s] (string/slice s 0 (min 1 (length s)))))
```

`(register NAME USAGE DOC F &opt TYPED)` calls F with the arguments, pipeline
input first. F returns a value, or nil for no value. By default every argument
must be text; use `map` to apply F to list items or `join` to turn a list into
text. With TYPED, F receives values as they are (see Embedding). F reports
failure with `(error "usage: ...")`; a predicate that does not hold calls
`(mismatch MSG)`.

Call `register` from Janet code that imports `src/cmdr.janet`, or from
`main.c` after it loads the module. A C function becomes a command the same
way: define it with `janet_cfuns`, then pass it to `register`, as `main.c`
passes its editor hooks to `use-editor`.

## Prelude

[src/cmdr.janet](src/cmdr.janet) provides the standard library in two layers:
`prelude` (text, numbers, files, programs) and `essentials` (collection and
composition commands). The application layer (`help`, `def`, and registered
commands) sits above them.

| Commands | Behavior |
| --- | --- |
| `text [ARG ...]` | Join positional arguments with spaces |
| `upper TEXT`, `lower TEXT` | Change letter case |
| `trim TEXT` | Remove surrounding whitespace |
| `length TEXT` | Return the byte length |
| `replace TEXT OLD NEW` | Replace all nonoverlapping literal matches; OLD must be nonempty |
| `list [VALUE ...]` | Construct an explicit list; empty arguments and embedded newlines remain intact |
| `lines LIST_OR_LINES` | Split text into a list of lines; lists pass through unchanged |
| `split TEXT SEPARATOR` | Split text into an explicit list; separator must be nonempty |
| `join LIST_OR_LINES SEPARATOR` | Join text items with a separator |
| `sort LINES` | Sort records by byte order |
| `uniq LINES` | Remove adjacent duplicates; use `sort \| uniq` for all duplicates |
| `head LINES N`, `tail LINES N` | Keep the first/last N records; N is a nonnegative integer |
| `filter LINES SUBSTRING` | Keep records containing a literal substring |
| `count LINES` | Return the number of records; fails on un-recovered failures |
| `add A B [NUMBER ...]`, `mul A B [NUMBER ...]` | Add/multiply numbers |
| `sub A B`, `div A B` | Subtract/divide in argument order |
| `read PATH`, `write VALUE PATH` | Read/replace text files; write returns the original value |
| `files [DIRECTORY]` | List regular file paths, sorted by byte order; defaults to `.` |
| `basename PATH`, `dirname PATH` | Extract a filename or parent path without accessing the filesystem |
| `map LIST_OR_LINES COMMAND [ARG ...]` | Apply a registered command to each item in order |
| `each LIST_OR_LINES PIPELINE [ARG ...]` | Run a pipeline per item, with the item as `$1` and ARGs as `$2...` |
| `keep LIST_OR_LINES PIPELINE [ARG ...]` | Keep items whose pipeline succeeds |
| `not VALUE PIPELINE [ARG ...]` | Return VALUE if the pipeline mismatches; mismatch if it succeeds |
| `lt NUMBER LIMIT`, `gt NUMBER LIMIT` | Numeric predicates; use `not` for `>=` and `<=` |
| `when VALUE COND THEN ELSE [ARG ...]` | Run THEN if COND holds, ELSE on a mismatch; errors propagate |
| `nonempty VALUE` | Predicate: VALUE is not empty text or an empty list |
| `equals TEXT EXPECTED`, `contains TEXT SUBSTRING` | Predicates on text |
| `take TEXT N`, `drop TEXT N` | Keep/drop the first N bytes |
| `slice TEXT START LENGTH` | Extract bytes at a zero-based offset |
| `append VALUE PATH` | Append to a file, returning the original value |
| `tee VALUE PATH` | Same as write: save a copy and return the original value |
| `try [INPUT] 'PIPELINE'` | Evaluate a pipeline and capture its failure |
| `recover VALUE FALLBACK` | Substitute FALLBACK after a captured failure; pass successful values through |
| `print TEXT` | Print text plus a newline, returning the text for further composition |
| `run PROGRAM [ARG ...]` | Execute a program with empty stdin, returning stdout |
| `run-in TEXT PROGRAM [ARG ...]` | Execute a program with TEXT on stdin, returning stdout |

```text
cmdr> text "  Hello World  " | trim | lower | replace world C
hello C
cmdr> text "pear,apple,pear,banana" | split , | sort | uniq | join ", "
apple, banana, pear
cmdr> text "pear,apple,pear,banana" | split , | filter pear | count
2
cmdr> add 2 3 | mul 4 | sub 1
19
cmdr> read greeting.txt | trim | upper | write greeting.txt
HELLO WORLD
```

`sort`, `uniq`, `filter`, `head`, `tail`, `count`, `join`, and `map` accept explicit
lists or newline-separated text. For text input, empty text represents zero
records and a terminal newline ends the last record without adding another.
Blank records are preserved. `sort`, `uniq`, `filter`, `head`, and `tail` preserve
the input representation: text in produces newline-terminated text; a list in
produces a list. `count` always returns a text number, but only after checking
that the input holds no captured failures — a failure cannot be discarded by
counting (use `map recover ""` first to substitute failed items).

`lines`, `list`, `split`, `files`, and `map` produce explicit lists. `split` preserves empty
fields, including trailing fields; splitting empty text returns an empty list.
It supports embedded newlines in fields and arbitrary nonempty separators.
`list ""` constructs a list containing one empty string; `list` constructs an empty
list. `join` joins text items and rejects nested lists. Write a newline as
`"\n"` (double quotes only), as in `join "\n"`.

### Mapping and files

```text
files ./logs | filter .log | map read | map upper
files ./logs | map basename | sort | join ", "
list "hello world" "a|b" "" | map upper | join ", "
list a,b c,d | map split , | map count | join ", "
text "hello world" | slice 6 5 | tee word.txt | append history.txt
```

`map` calls the named command once per item, passing the item as its first
argument followed by the supplied arguments. It uses the active registry,
including custom commands and overrides; values are passed directly, without
building or evaluating source code. Each result remains one item, even if it
contains newlines or is itself a list. Calls execute sequentially; an uncaught
failure or a missing return value stops mapping and reports the item number.
Earlier effects remain. Empty input returns an empty list without calling the
command, but the command name is still validated.

`files` lists the regular files directly inside a directory, including hidden
files and symlinks to regular files. It excludes directories, skips dangling
symlinks, and does not recurse. Paths contain the supplied directory prefix.
Names containing spaces or newlines remain single items. `basename` and `dirname`
are lexical operations: they trim trailing slashes, use `/` for root, and use `.`
for an empty path or a parentless filename.

`take`, `drop`, and `slice` accept nonnegative integers. Counts beyond the input
are clamped; an offset past the end returns empty text. They operate on bytes.
`append` adds exactly the supplied text, without inserting a newline. `write`,
`tee`, and `append` also accept lists: they serialize items with newline
terminators for the file, then return the original typed value. Serialization
and REPL display are text exports; use the typed API to retain list boundaries.

### Pipelines per item and predicates

```text
list a bb '' | keep nonempty | join ,                => a,bb
files . | keep 'read | contains TODO'
files . | keep 'read | not "contains TODO"'
list a b c | keep 'not "equals $2" $2' b | join ,    => a,c
list 'a b' c | each 'text $2 $1' pre | join ,        => pre a b,pre c
text '   ' | trim | try 'nonempty | upper' | recover none
```

`each` and `keep` take a pipeline string, unlike `map`, which takes a command
name. The item is `$1` and feeds the first stage; extra arguments are `$2...`.
A nested string has its own `$1..$N`, so pass outer parameters explicitly, as
in `'not "equals $2" $2'`, or bind them with `as` and use the name. `each` returns a list. `keep` preserves the input
representation, like `filter`.

`when VALUE COND THEN ELSE [ARG ...]` runs COND with VALUE as `$1` and ARGs
as `$2...`, then THEN if COND holds or ELSE on a mismatch. An error in COND
is reported, not treated as false; `try` cannot make that distinction. Each
pipeline receives only the parameters it references, and every ARG must be
referenced by at least one of them. ELSE is required: without it, use
`COND | THEN`.

```text
def clean-or 'trim | when nonempty upper "$2" $2'
text a | when 'equals $2' 'text yes $1' 'text no $1' a      => yes a
```

Predicates return their input when they hold, so they compose mid-pipeline.
When they do not hold, they fail with the status `CMDR_MISMATCH`. `keep` and
`not` treat only that status as false. Any other failure, including one nested
inside the pipeline (a usage error, a missing file), is an error. Their
pipeline's syntax, command names, and arity are checked before any item runs.
Truth comes only from success or mismatch; empty text and `"0"` are not
false. `try`/`recover` capture mismatches like other failures, which gives
branching. `each` reports any failure, so its pipeline must recover captured
failures itself; use `map try` to carry failures per item.

### Recovery

```text
try 'read optional.txt' | recover default | upper
text optional.txt | try read | recover default
try 'run grep ERROR app.log' | recover "" | count
list first.txt second.txt | map try read | map recover "" | map length
```

`try` evaluates its quoted pipeline with the active registry. When piped into,
it forwards the input to the first stage of that pipeline. On success it returns
the original result; on failure it produces a captured failure value. Subsequent
ordinary commands skip that failure until `recover` replaces it with a fallback.
Recovery leaves successful values untouched. `map try read` captures each file's
failure separately; `map recover ""` replaces failed items while preserving
successful contents. An uncaptured failure stops immediately, and any captured
failure left at the end (including inside a list) makes evaluation fail.

The outer pipeline's syntax and command names are checked before execution.
Errors inside the quoted `try` pipeline, including unknown command names and
syntax errors, can be recovered. Neither `try` nor `recover` rolls back effects.

A captured failure is an ordinary value while it is being carried: commands that
consume a whole list (such as `count`) fail if the list still contains one, so a
failure is never silently discarded by counting or joining. Commands that
require text items report the captured failure's message rather than a generic
"expected text" error. Substitute first with `map recover ""` when a failed item
is acceptable.

Text operations work on bytes using the C locale, without Unicode case folding.
Arithmetic uses finite double precision numbers and reports invalid numbers,
overflow, and division by zero. `print` performs an immediate effect, so using
it as the final stage prints the value twice (once immediately, once by the REPL).

### OS executables

`run` starts an executable and returns its stdout as a pipeline value. `run-in`
also sends the first argument to the executable's stdin, which fits the existing
pipeline rule:

```text
cmdr> run printf '%s' 'hello world' | upper
HELLO WORLD
cmdr> text "hello world" | run-in tr a-z A-Z
HELLO WORLD
cmdr> read app.log | run-in grep ERROR | sort | uniq | write errors.log
cmdr> run ls -1 | filter .c | count
```

Programs are resolved through PATH (or an explicit executable path), inherit
the working directory and environment, and receive each positional argument
unchanged. There is no implicit shell: glob patterns, dollar signs, and other
shell syntax are literal arguments. Stderr goes directly to the REPL's stderr.
`run` supplies empty stdin so a child cannot consume subsequent REPL commands.
`run-in` supplies exactly the input text, without adding a newline.

Execution is synchronous. Input and stdout use temporary files to avoid pipe
buffer deadlocks; stdout is returned in full after the executable exits.
These commands suit noninteractive text programs; they do not provide a terminal
or stream output live. NUL bytes in stdout are rejected, matching the text value
model. A missing executable, nonzero exit status, or terminating signal fails
the stage and prevents subsequent stages from running. Captured stdout from a
failed command is discarded; effects already performed by it remain. Programs
such as `grep` return a nonzero status when there are no matches, so that also
stops the pipeline.

### Embedding

[src/cmdr.janet](src/cmdr.janet) is a Janet module. Import it, or load it into
an environment as `main.c` does, and call:

| Function | Behavior |
| --- | --- |
| `(eval-line LINE)` | Evaluate a pipeline; return its value or raise its error |
| `(register NAME USAGE DOC F &opt TYPED)` | Add an application command |
| `(command-names)` | Active command names, newest definition first |
| `(mismatch? ERR)`, `(message ERR)` | Classify an error; get its text |
| `(failure? VALUE)` | Whether VALUE is a captured failure |
| `(use-editor READ REMEMBER)` | Read interactive lines through a host editor |
| `(main & ARGS)` | The command-line driver; exits the process |

```janet
(import ./src/cmdr :as cmdr)
(cmdr/eval-line "list 'a\nb' c | map upper")   # => ("A\nB" "C")
```

Text is a string, a list is a tuple, and no value is nil. A predicate that does
not hold raises `{:mismatch MSG}`; other errors raise a string. `eval-line`
raises when a captured failure remains in the result, including inside a list.
Composed commands nest at most `max-depth` (128) levels; deeper nesting is an
error, not a stack overflow.

### Checks

Run `make check` or `make test` (or `cmake --build build --target check`) for
the integration checks (`tests/test_cmdr.py`, Python 3) and the module checks
(`tests/test_api.janet`, run by a `janet` executable built from
`thirdparty/janet`, or found in `CMDR_JANET_DIR`). Each is skipped if its
interpreter is not found at configure time. Tests are
enabled by default and can be turned off with `-DCMDR_BUILD_TESTS=OFF`.
