# cmdr

A small C REPL for commands with positional arguments, text and list values,
and explicit effects.
Requires a C11 compiler and CMake 3.16+ on Linux/macOS, using POSIX-2008
facilities (`getline`, `posix_spawn`, `fstatat`/`dirfd`, `F_DUPFD_CLOEXEC`).
Make is an optional frontend for the same CMake build.
The interactive REPL uses [linenoise](../linenoise) when a checkout is found
at `../linenoise`; set `-DCMDR_LINENOISE_DIR=PATH` to use another, or to empty
to read plain lines. It adds:

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
Exit with Ctrl-D; Ctrl-C discards the current line or continuation. For scripts, pass a pipeline
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
with the same parameter rules as `.pipeline` (see below). The body's syntax and
command names are checked when it is defined, so define helpers before the
commands that use them. Arity is checked per call. A definition may shadow any
command except `def` and `as`, including earlier definitions; `help` marks
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
definitions report a call chain (`outer: inner: equals: ...`). Definitions
belong to the example REPL in `main.c` and `commands.c`; the embeddable core
stays immutable. Set `.scope` on a `cmdr_command` for the same lexical
resolution in C.

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
background execution. Text values are NUL-terminated strings, not binary buffers.

## Adding C commands

Define commands in [src/commands.c](src/commands.c): add an entry to the
`commands` table in `cmdr_commands_init`. Each entry sets exactly one of
`.run`, `.run_value`, or `.pipeline`. [include/commands.h](include/commands.h)
exposes that initializer, which `main.c` calls to load the command registry and
prelude automatically. Application commands override prelude commands with the
same name; `help` lists each active name once. Arguments exclude the command
name. Set `.usage` to the command syntax and `.docstring` to its help
description. `help` automatically aligns descriptions across all active commands;
omitting `.usage` falls back to the command name, and `.docstring` is optional.

To define a command as a pipeline of other commands, set `.pipeline`:

```c
/* In the commands table: */
/* {.name = "clean", .usage = "clean [TEXT]",
    .docstring = "Trim and lowercase text", .pipeline = "trim | lower"}, */
/* {.name = "shout", .usage = "shout [TEXT]",
    .docstring = "Uppercase text", .pipeline = "upper"}, */
```

`text "  Hi  " | clean` and `clean "  Hi  "` both evaluate the body with that
value as pipeline input. Bodies
resolve names through the active registry, so they see application overrides
and can call other pipeline commands. Set `.handles_failure` when the body
should consume a captured failure (for example a body that starts with
`recover`).

Bodies take parameters as `$1..$N`. `$1` is the first argument, piped or
explicit. A body whose highest reference is `$N` requires exactly N arguments;
a body without references accepts at most one. The first stage receives `$1`
as input unless it references `$1` itself. References substitute typed values,
not text: a list stays one argument, and a value containing `|` is not parsed.

```c
/* {.name = "wrap", .usage = "wrap TEXT MARK", .pipeline = "text $2 $1 $2"}, */
/* {.name = "swap", .usage = "swap TEXT OLD NEW", .pipeline = "replace $2 $3"}, */
```

With those entries added:

```text
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

`as NAME` binds the current value to `$NAME` for the rest of the pipeline and
passes it through. Bindings are immutable and must precede their use; names
start with a letter and contain letters, digits, `_`, or `-`.

```text
cmdr> list b a b | as lines | count | as total | $lines | sort | uniq | count | text of $total
2 of 3
cmdr> text v | as x | text a $x b
a v b
```

The current value is named while it is `$1` (at the start), a lone reference,
or just bound by `as`. A stage that references that name receives the value
only where named; any other stage receives it as its first argument. So
`text $2` as a first stage still receives `$1`; write `$2 | text` or name `$1`.

`try` strings bind their input as `$1`. A nested string has its own `$1..$N`
but sees the enclosing pipelines' `as` bindings, innermost first. A pipeline
command's body does not see its caller's bindings. `$_` is reserved. A reference without an argument or binding, including `$1` at the
top level, is an error. See [docs/dev/compose-ng.md](docs/dev/compose-ng.md) for the
design.

For custom C logic, implement a `cmdr_fn` from [include/cmdr.h](include/cmdr.h):

```c
static int first(void *user, size_t argc, const char *const *argv,
                 char **result, char **error)
{
    (void)user;
    if (argc != 1) return cmdr_error(error, "usage: first TEXT");
    *result = malloc(2);
    if (!*result) return cmdr_error(error, "out of memory");
    (*result)[0] = argv[0][0];
    (*result)[1] = '\0';
    return 0;
}

/* In the commands table: */
/* {.name = "first", .usage = "first TEXT",
    .docstring = "Return the first byte", .run = first}, */
```

Return zero on success. Set `*result` to a fresh malloc-owned string, or leave it
NULL for no value. Return nonzero and use `cmdr_error` on failure. The interpreter
frees both outputs; do not return borrowed arguments or static strings. String
callbacks receive only text arguments; use `map` to apply them to list items or
`join` to turn a list into text. The
opaque `user` pointer lets commands access application state. The interpreter
in `cmdr.c` can also be embedded independently of the example REPL in `main.c`.

## Prelude

[include/prelude.h](include/prelude.h) / [src/prelude.c](src/prelude.c) provide
the standard library, with collection and composition commands in
[src/essentials.c](src/essentials.c), all loaded automatically by
`cmdr_commands_init`. Custom commands belong in `commands.c`, so the prelude can
be reused across applications.

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
def clean-or '$2 | as fallback | $1 | trim | when nonempty upper "$fallback"'
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

`make` also builds `build/libcmdr.a`, containing the interpreter and prelude.
Include `prelude.h` and link your application against that library:

```c
cmdr repl = { .parent = cmdr_prelude() };
char *result, *error;
int status = cmdr_eval(&repl, "add 2 3 | mul 4", &result, &error);
/* Handle status; free(result) and free(error). */
```

To override or extend the prelude, set `.commands` and `.count` to your own command
table while retaining `.parent = cmdr_prelude()`. The prelude has static storage
and needs no initialization or cleanup. Leave `.parent` NULL for an interpreter
with only your commands. Each registry in a parent chain must outlive evaluation;
parent chains must not contain cycles. Composed commands must also not cycle:
`.pipeline` bodies, `map`, and `try` are evaluated through the same interpreter,
and runaway nesting is detected and reported as an error (`possible cycle in
composed commands`) rather than looping or overflowing the stack.

For explicit values, use `cmdr_eval_value` and release results with
`cmdr_value_free`:

```c
cmdr_value result = {0};
char *error = NULL;
int status = cmdr_eval_value(&repl, "list 'a\nb' c | map upper", NULL,
                              &result, &error);
/* On success: result.kind == CMDR_LIST, result.count == 2. */
cmdr_value_free(&result);
free(error);
```

`cmdr_eval` remains available as a text interface and serializes final lists as
newline-delimited text. Use `cmdr_call` to invoke a registered command directly
with values, bypassing parsing. `cmdr_eval_args` evaluates a pipeline with
typed values bound to `$1..$N`; `cmdr_check` validates a pipeline for a given
argument count without running it. A predicate callback returns
`CMDR_MISMATCH` (with an error message) when it does not hold, and 1 on error. For commands that consume/produce typed values,
set `.run_value` to a `cmdr_value_fn` instead of setting `.run`. Its `repl` argument
is the active registry, and its positional arguments include pipeline input as
the first value. Values passed to callbacks are borrowed. Return owned values
through `result`, using the clone, text, push, and free helpers in `cmdr.h`.
Initialize values with `{0}`; output values must not alias input values.

A `.run_value` callback can call other commands with `cmdr_call` and use C for
branching, loops, and intermediate values. `clean-or TEXT FALLBACK` trims its
input, returns FALLBACK when the result is empty, and otherwise uppercases it:

```c
static int clean_or(const cmdr *repl, void *user, size_t argc,
                    const cmdr_value *argv, cmdr_value *result, char **error)
{
    (void)user;
    if (argc != 2 || argv[0].kind != CMDR_TEXT || argv[1].kind != CMDR_TEXT)
        return cmdr_error(error, "usage: clean-or TEXT FALLBACK");

    cmdr_value trimmed = {0};
    int status = cmdr_call(repl, "trim", 1, argv, &trimmed, error);
    if (status) goto done;

    /* Overrides can return typed values, including captured failures. */
    if (trimmed.kind != CMDR_TEXT) {
        status = cmdr_error(error, "clean-or: trim must return text");
        goto done;
    }
    if (trimmed.text[0] == '\0')
        status = cmdr_value_clone(&argv[1], result, error);
    else
        status = cmdr_call(repl, "upper", 1, &trimmed, result, error);

done:
    cmdr_value_free(&trimmed);
    return status;
}

/* In the commands table: */
/* {.name = "clean-or", .usage = "clean-or TEXT FALLBACK",
    .docstring = "Trim and uppercase text, or return FALLBACK when empty",
    .run_value = clean_or}, */
```

```text
cmdr> clean-or '  hello  ' '(empty)'
HELLO
cmdr> text '   ' | clean-or '(empty)'
(empty)
```

`argv` is borrowed, so the fallback branch clones it. `trimmed` is owned and is
freed on every path; `result` is owned by the caller. Earlier calls' effects are
not rolled back on failure. For a fixed pipeline, use
`cmdr_eval_value(repl, "trim | upper", &argv[0], result, error)` instead.

### Checks

Run `make check` or `make test` (or `cmake --build build --target check`) for integration
checks and C embedding checks. Python 3 is used for the integration checks; if
Python 3 is not found at configure time, `test_cmdr` is skipped (the C embedding
checks still run). Tests are enabled by default and can be turned off with
`-DCMDR_BUILD_TESTS=OFF` — Python is never required to simply build or run cmdr.
