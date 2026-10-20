# cmdr

A small C REPL for commands with positional arguments, text and list values, and explicit effects. Requires a C11 compiler and CMake 3.16+ on Linux or macOS, using POSIX-2008 (`getline`, `open_memstream`, `posix_spawn`, `fstatat`). The `dir` option of `run-with` also needs `posix_spawn_file_actions_addchdir`, or its older `_np` form (glibc 2.29, macOS 10.15); without either, `dir` is an error. Make is an optional frontend for the same CMake build.

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

Run `help` for the command list; it returns text, so `help | filter map` works. `help NAME` shows one command's full usage and documentation. Exit with `quit`, `exit`, or Ctrl-D.

The REPL edits lines with a vendored copy of linenoise, in [thirdparty/linenoise](thirdparty/linenoise). The prompt is light green, except on terminals without escape sequences (`dumb`, `cons25`, `emacs`). It colors commands, references, blocks, and strings, and shows unknown commands in red. Tab completes command names. Up and Down recall history, saved in `~/.cmdr_history`, with a continued pipeline stored as one entry. Ctrl-C drops the current pipeline. Pasted lines run one at a time, each echoed after its prompt. Configure with `-DCMDR_EDITOR=OFF` to read plain lines instead.

- `./cmdr -c 'PIPELINE'` evaluates one pipeline.

- `./cmdr FILE [ARG ...]` runs a script and stops at its first failing line, reporting `FILE:LINE:`. `script-args` in the script returns the ARGs as a list.

- Piped stdin runs one pipeline per line and continues after errors.

- `load PATH [ARG ...]` runs a script in the current session, so its definitions remain. The script sees ARGs through `script-args`. A relative PATH resolves from the current directory, not the script's, as in a shell. To load a file next to the script, write `script-dir | path-join lib.cmdr | load`. `import` searches the script's directory first.

- `import NAME [as ALIAS]` runs a module once per session and binds its definitions as `NAME.DEF`. `from NAME import DEF ...` binds the DEFs listed without the prefix. See [Modules](#modules).

- `script-file` returns the absolute path of the script file in which it is written, like `realpath(__file__)` in Python. `script-dir` returns that file's directory, and `script-args` the arguments it was run or loaded with, like `sys.argv[1:]`. A `def` in `lib/x.cmdr` whose body uses `script-dir` returns `lib`, even when a script elsewhere calls it. In the REPL, `-c`, and embedded scripts all three are errors.

- `./cmdr --user ...` runs `~/.cmdr/prelude.cmdr` instead of the standard prelude; it is an error if the file is missing.

- `./cmdr --version` prints the version.

The REPL starts with a line such as `cmdr 0.1.0 (prelude: standard)`, or the prelude's path with `--user`, in bold cyan where the prompt has color.

Errors go to stderr; the exit status is nonzero if any line failed. Ctrl-C while a line runs stops it, and cmdr exits by SIGINT, so a calling shell stops too. A running program gets SIGTERM, then SIGKILL a second later, so one that ignores SIGINT does not outlive cmdr. Lines starting with `#`, including a `#!/usr/bin/env cmdr` shebang, are comments. A line ending in an unescaped `|` continues on the next line; blank and comment lines inside a continuation are skipped. Quotes and blocks cannot span lines otherwise: write a newline as `\n`, and end a line inside a block with `|` to continue it.

## Composition

`a | b x` evaluates `a`, then calls `b` with the returned value followed by `x`. The value stays one argument, even when it contains spaces, newlines, or pipes. A command returns text, a list, or no value; no value cannot feed another command. The REPL prints text as is and list items one per line.

Whitespace separates arguments. Single and double quotes preserve spaces and pipes, including empty arguments (`""`). Backslash escapes the next character outside single quotes; inside double quotes, `\n` and `\t` are a newline and a tab. Syntax and command names are checked for the whole pipeline before any command runs. `map`'s COMMAND is an argument, so it is looked up only when `map` runs; `each { COMMAND }` is checked first. Effects from earlier stages remain if a later stage fails. There is no variable expansion, globbing, redirection, or implicit shell.

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

- A parameter after the first may have a default: `{ (chain amount=50) ... }`. A call may omit it, and parameters after it need defaults too. A default is one word without spaces, quotes, or `)`, or `""` for empty text. The `help` overview shows a definition's defaulted parameters as `...`, and `help NAME` shows each as `[AMOUNT=50]`. See [docs/dev/defaults.md](docs/dev/defaults.md).

  ```text
  cmdr> def greet { (name greeting=hello) text $greeting $name }
  cmdr> greet bob
  hello bob
  cmdr> greet bob hi
  hi bob
  ```

- Declared parameters appear only where named, and each must be used. Only `{ ... }` pipes its input into its first stage, so a block can start from something else: `{ (w) read app.log | contains $w }`.

- Each later stage receives the previous result first. A stage that is a lone `$name` makes that value the result.

- Names are lexical. A nested block sees the names of enclosing blocks, and an inner name shadows an outer one. A block keeps the values it captured.

- References substitute values, not text: a list stays one argument. Quote or escape `$` and braces for literal text; `a$x` is literal. Unquoted braces inside a block must balance.

`def NAME BLOCK [DOC]` defines a command for the rest of the session. `help` shows DOC, or the body if DOC is absent. Definitions are lexical: a body uses the commands that existed when it was defined, and a body that names its own command calls the one it shadows. A definition may shadow any command except `def`. Errors from a body are prefixed with its name (`outer: inner: equals: ...`). A mismatch from a def called on the line also shows the call's arguments: `ends-with ar bar: ge: 2 is not at least 3`.

`each`, `keep`, `not`, and `try` take blocks. Extra arguments follow the item: `list a b c | keep { (s v) not $s { equals $v } } b`.

## Commands

These are the primitives, in [src/primitives.c](src/primitives.c). Extensions add commands named after them, such as `re-find` and `num-seq`; see [Extensions](#extensions).

| Commands | Behavior |
| --- | --- |
| `text [ARG ...]` | Join arguments with spaces |
| `upper`, `lower`, `trim`, `length` TEXT | Case, whitespace, byte length |
| `replace TEXT OLD NEW` | Replace all literal matches; OLD is nonempty |
| `take TEXT N`, `drop TEXT N` | Keep or drop the first N bytes |
| `split TEXT SEPARATOR` | Split into a list; empty fields are kept |
| `add`, `mul` A B [NUMBER ...]; `sub`, `div` A B | Double-precision arithmetic |
| `list [VALUE ...]`, `lines VALUE` | Build a list; split text into lines |
| `push VALUE [ITEM ...]` | Append items to a list |
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
| `files [DIR]`, `path-basename PATH`, `path-dirname PATH` | Regular files in DIR, sorted; lexical path parts |
| `run PROGRAM [ARG ...]`, `run-in TEXT PROGRAM [ARG ...]` | Run a program, returning stdout |
| `run-with OPTIONS PROGRAM [ARG ...]` | Run a program with options, returning `STATUS STDOUT STDERR` |
| `load PATH [ARG ...]` | Run a script in this session; PATH is relative to the cwd |
| `import NAME [as ALIAS]` | Run a module once; bind its definitions as `NAME.DEF` |
| `from NAME import DEF [as ALIAS] ...` | Run a module once; bind each DEF unqualified, if the name is new |

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

List commands accept lists or newline-separated text; in text, a final newline ends the last item. `sort`, `uniq`, `head`, `tail`, `filter`, and `keep` return text for text input and lists for lists, so `read app.log | filter ERROR | upper` works. `lines`, `split`, `map`, `each`, `list`, and `files` return lists. Text commands reject lists; use `map` per item or `join` first. `write` and `append` store lists one item per line.

**Predicates** return their input when they hold, so they compose mid-pipeline. When they do not hold they fail with a mismatch, which `keep`, `not`, and `try` treat as false. Any other failure is an error, including one inside the block. Empty text and `"0"` are not false.

**`try`** returns the block's result, or FALLBACK if it fails at run time. Its arity is checked first, so a typo or a missing input is an error, not a fallback. `each { try { read } "" }` reads each file or substitutes empty text.

**`or`** is `try` for predicates: only a mismatch selects FALLBACK, and errors propagate. `with` passes a computed value to any argument position: `length $x | with { (n) drop $s $n }`. `check` replaces a mismatch's message, so a composed predicate can report `b is not a` instead of the commands it is built from.

**`run`** resolves PROGRAM through PATH and passes arguments unchanged. `run` supplies empty stdin; `run-in` supplies TEXT. Output is returned after the program exits. A nonzero exit status, such as `grep` without matches, fails the stage. The program's stderr is not captured; it goes to cmdr's stderr.

**`run-with`** captures stderr and returns the exit status as data. OPTIONS is a list of NAME VALUE pairs: `dir` sets the working directory, `stdin` the input, and `timeout` a limit in seconds. Past the limit the program gets SIGTERM, then SIGKILL a second later; its own children are not signalled. STATUS is the exit status, 128 plus the signal number, or `timeout`. A list argument adds its items as arguments, at any depth, so an empty list adds none:

```text
cmdr> list dir / | with { (o) run-with $o sh -c 'pwd; echo warn >&2; exit 3' } | each { trim } | join ,
3,/,warn
cmdr> list | with { (o) list --model opus | with { (m) run-with $o printf '%s ' $m x } } | nth 2
--model opus x
```

Text is bytes in the C locale. Files and program output containing NUL bytes are rejected. Numbers are decimal, such as `-1.5e3`; hex and surrounding spaces are rejected. Results print with the fewest digits that read back exactly: `div 1 3` gives `0.3333333333333333`.

## Growing a prelude

Start with a pipeline. This one lists the distinct file extensions in the current directory:

```text
cmdr> files | map path-basename | keep { drop 1 | contains . } | each { split . | tail 1 | join "" } | sort | uniq
md
txt
```

`keep { drop 1 | contains . }` keeps names with a dot after the first byte, so it skips `Makefile` and `.gitignore`. `tail 1 | join ""` takes the last item.

[src/prelude.cmdr](src/prelude.cmdr) names the parts, each built on the ones before. It also defines list, number, text, and path commands, such as `nth`, `sum`, `max`, `starts-with`, `path-stem`, `path-join`, and `path-split`; `help` lists them. These are three of its definitions:

```text
def last { nonempty | tail 1 | join "" } "Return the last item"
def path-ext { path-basename | check { drop 1 | contains . } "no extension" | split . | last } "Return the text after the basename's last dot; mismatch if it has none"
def path-has-ext { keep { drop 1 | contains . } } "Keep names with a dot after the first byte"
```

A session builds on them:

```text
cmdr> def exts { (dir) files $dir | map path-basename | path-has-ext | map path-ext | sort | uniq } "List extensions in DIR"
cmdr> exts .
md
txt
```

Put definitions in a file, such as `ext.cmdr`, and run `load ext.cmdr` in a session or at the top of a script. Move a definition into `src/prelude.cmdr` when every session should have it, or into `src/prelude-effects.cmdr` if it reads files or runs programs. The build embeds the file, and cmdr runs it at startup; an error there stops cmdr with `prelude.cmdr:LINE:`. Because definitions are lexical, a later `def tail` does not change the prelude's `last`.

## Modules

`import NAME` follows Python's `import`. It searches, in order:

1. the directory of the file in which `import` is written, or the current directory in the REPL and `-c`;
2. each directory in `CMDR_PATH`, separated by `:`;
3. `~/.cmdr/lib`;
4. `share/cmdr/lib` under the install prefix.

In each directory, a package `NAME/__init__.cmdr` wins over `NAME.cmdr`. The module runs once per session, with no arguments and without printing its values. Later imports bind its names again. The importer gets `NAME.DEF` for each definition, or `ALIAS.DEF` with `as`. The module's other names, including its own imports, stay inside it.

```text
cmdr> import aw
cmdr> read bug.md | aw.target-bug ~/src/app | aw.fix
```

`from NAME import DEF [as ALIAS] ...` imports the module the same way, then binds only the DEFs listed, as `DEF` or `ALIAS`. A name that already exists is an error, and then the statement binds nothing. That includes a primitive, an extension command, a prelude definition, or an earlier `def`. Binding over it would change what later lines call without warning. `as` gives the definition another name. Binding the same definition to the same name again is allowed, and a later `def` may still shadow it. There is no `from NAME import *`.

```text
cmdr> from snd import in rate out trim
cmdr: from snd import trim: trim already exists; bind it with as
cmdr> from snd import in rate out trim as cut
cmdr> in a.wav | rate 48000 | cut 0 1 | out b.wav
b.wav
```

A package finds its own files with `script-dir`. Design and departures from Python: [docs/dev/import.md](docs/dev/import.md).

## Adding commands

Write a command in C only when it cannot be composed. For the `cmdr` executable, write an [extension](#extensions). This section is for a host that embeds cmdr. The build produces `libcmdr.a`, with the API in [include/cmdr.h](include/cmdr.h). A host puts its commands in a table in front of the primitives in [src/primitives.c](src/primitives.c), where they shadow them, and opens a session on it:

```c
static int initial(const cmdr *repl, void *user, size_t argc, const cmdr_value *argv,
                   cmdr_value *result, char **error)
{
    if (argc != 1) return cmdr_error(error, "usage: initial TEXT");
    *result = cmdr_text(argv[0].text, argv[0].text[0] ? 1 : 0);
    return 0;
}

static const cmdr_command commands[] = {
    {.name = "initial", .usage = "initial TEXT", .docstring = "Return the first byte",
     .run = initial, .text = 1},
};
static const cmdr table = {commands, 1, &cmdr_primitives};

cmdr_session *session = cmdr_session_open(&table, 1, stdout);
char *error;
cmdr_value value;
if (cmdr_session_prelude(session, &error)
    || cmdr_session_eval(session, "list ab cd | map initial", &value, &error))
    fprintf(stderr, "%s\n", error);
```

`.text = 1` guarantees text arguments, and `.user` is passed to `.run`. Arguments are borrowed, including the values a block captured; clone what you keep. The result is owned by the caller. A predicate returns `CMDR_MISMATCH` when it does not hold; `cmdr_describe` gives a short form of a value for its message.

A session adds `help` and `def`, and frees its definitions on `cmdr_session_close`. `cmdr_session_load` runs a script and stops at its first failure. Sessions on different threads are independent. [src/main.c](src/main.c) is the cmdr executable, built on this API.

The second argument of `cmdr_session_open` allows effects. With it, the session adds `load` and `import`, and `cmdr_session_prelude` also runs [src/prelude-effects.cmdr](src/prelude-effects.cmdr), which defines `cat`, `grep`, `sh`, and `sh-in`. For a console without file or process access, pass 0 and end the host's table in `&cmdr_pure`, which omits `read`, `write`, `append`, `files`, `run`, and `run-in`.

cmdr parses and prints numbers with `.` only while `LC_NUMERIC` is `"C"`, the default. A host that calls `setlocale` must leave that category alone.

Allocation failure exits the process with status 1. `cmdr_set_out_of_memory` installs a handler that runs first, to log or save host state.

`cmdr_interrupt(1)`, from a SIGINT handler, makes every later command call fail with `interrupted`, which `try` does not catch, and stops a program that `run` is waiting for. The flag is process-wide; the host clears it with `cmdr_interrupt(0)`.

## Extensions

An extension adds C commands to the `cmdr` executable without editing `src/main.c`. Each is a directory `ext/NAME/`:

- `*.c`: the commands. One file defines `const cmdr cmdr_ext_NAME`, with `-` in NAME as `_`.

- `*.cmdr`: definitions, embedded by the build and run in name order.

- `README.md`: what the extension does. The build fails without it.

- `tests/`: at least one `*.cmdr` or `*.c` file; configure fails without one. `make test` runs each `.cmdr` file with `cmdr`, in `tests/`, and fails at its first failing line. Each `.c` file is a program linked with the extension's sources and `libcmdr.a`, for checks a script cannot make, such as error messages. Configuring with `-DCMDR_BUILD_TESTS=OFF` skips both.

`scripts/mk_ext.py NAME` creates `ext/NAME/` with these files and a command `NAME TEXT`, which returns TEXT. The skeleton builds and passes its tests, so edit from there. `--c-test` also creates `tests/test_NAME.c`. It refuses an existing directory. `make test` builds a fresh skeleton, so a template change that breaks it fails there.

[ext/env](ext/env/README.md) is an example:

```c
static const cmdr_command commands[] = {
    {.name = "env", .usage = "env NAME", .docstring = "Return an environment variable",
     .run = env_command, .text = 1},
};
const cmdr cmdr_ext_env = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
```

Each command is named NAME or starts with `NAME-`, as `re-find` does. This keeps names from colliding with each other and with later primitives.

The extensions in `ext/`:

| Extension | Commands |
|-|-|
| [env](ext/env/README.md) | Environment variables |
| [fs](ext/fs/README.md) | Path tests, sizes, times, `fs-mkdir`, `fs-remove`, `fs-move` |
| [num](ext/num/README.md) | `num-seq`, modulo, rounding |
| [re](ext/re/README.md) | Regex extraction, splitting, and replacement |
| [shlex](ext/shlex/README.md) | Shell-style word splitting and quoting, as Python's `shlex` |
| [str](ext/str/README.md) | Byte offsets, repetition, padding, reversal |
| [time](ext/time/README.md) | The current time, and strftime formatting |

Extension C code can use `cmdr_number`, `cmdr_number_value`, and `cmdr_natural` to parse and print numbers as primitives do. Test programs include `tests/ext_check.h` for `CHECK`, `expect`, and `expect_error`.

`help` shows an extension's C commands with `ext/NAME:` before the docstring. Its definitions show their own docs.

Extension names must be new. cmdr fails at startup if an extension command, in C or `.cmdr`, has the name of a built-in, a primitive, another extension's command, or a prelude definition. Prelude definitions bind names when defined, so a shadowed name would change them silently. A session `def` may still shadow an extension command.

Extension scripts run before the prelude, so they may use only primitives and extension commands. `--user` replaces the prelude, and a script that used it would fail only there. Extensions are part of the executable, not `libcmdr`; an embedding host adds commands as in [Adding commands](#adding-commands).

## Installing

```sh
cmake -S . -B build && cmake --build build
cmake --install build --prefix /usr/local
```

This installs `cmdr`, `libcmdr.a`, `libcmdr.so`, `cmdr.h`, a CMake package, and `cmdr.pc`. A CMake host uses `find_package(cmdr)` and links `cmdr::static` or `cmdr::shared`; others use `pkg-config --cflags --libs cmdr`. `-DCMDR_SHARED=OFF` builds only the static library. The shared library's soname is `libcmdr.so.0.1`, since a minor version may break the API before 1.0.

Syntax highlighting for Vim, Sublime Text, and VS Code is in [editors/](editors/README.md).

## Checks

`make test` builds cmdr and runs [tests/test_cmdr.py](tests/test_cmdr.py) with Python 3, in parallel. It also runs each extension's `tests/`. It runs [tests/test_api.c](tests/test_api.c) against both libraries, then installs into `build/staged` and builds [tests/consumer](tests/consumer) against it. Each test method is a CTest test, so `ctest --test-dir build -R test_editor` runs only the editor tests. Tests that need the editor show as skipped in a build without it. Configure with `-DCMDR_BUILD_TESTS=OFF` to build without Python.

Design history: [docs/dev/minimal.md](docs/dev/minimal.md), [docs/dev/blocks.md](docs/dev/blocks.md).
