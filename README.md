# cmdr

`cmdr` is a small glue language and REPL in C. It is designed around the the idea that a script consists of pipelines of commands over text and lists, with explicit effects.

```text
cmdr> text "hello world" | upper | write greeting.txt
HELLO WORLD
cmdr> read greeting.txt | lower
hello world
cmdr> read greeting.txt | split " " | each { (w) lower $w } | join -
hello-world
cmdr> def dashed { (s sep=-) split $s " " | each { (w) lower $w } | join $sep }
cmdr> read greeting.txt | dashed +
hello+world
```

Pipelines look like Unix pipes. They differ in five ways:

- `|` passes a value, text or a list, as the next command's first argument. Numbers are text that arithmetic commands parse. `$(...)` computes an argument in place. Values are never re-parsed: no word splitting, globbing, or implicit shell.

- Blocks, `{ (a b) ... }`, pass code as values, with lexical scope and captured values. `each`, `keep`, `fold`, `try`, and `retry` take blocks, so loops and error handling stay inside the pipeline.

- Syntax, command names, and references are checked before any stage runs. A typo fails the line before an earlier stage writes a file.

- Predicates mismatch rather than fail. A test composes mid-pipeline, and `if`, `keep`, and `or` tell a mismatch from an error. A Unix exit status conflates the two.

- Effects are explicit. Programs run only through `run` and its variants. `run` fails the stage on a nonzero exit status; `run-with` returns the status as data.

The language also has `def`, which names a command; `as`, which names a value; `if`/`elif`/`else`; and modules, loaded with `import` and `from`. Extensions add C commands as modules. Where Python has a counterpart, cmdr follows it: `import`, `random`, `shlex`, `str.format`.

Start with [Quick start](#quick-start), then [The language](#the-language).

## Quick start

cmdr needs a C11 compiler and CMake 3.16 or later, on Linux or macOS. Make is an optional frontend for the same CMake build.

```sh
make
./build/cmdr
```

Run code in three ways:

- `./build/cmdr` starts the REPL. `help` lists the commands; `quit`, `exit`, or Ctrl-D leaves. `quit` and `exit` work only on a terminal; piped input ends at end of file.

- `./build/cmdr FILE [ARG ...]` runs a script.

- `./build/cmdr -c 'PIPELINE'` runs one pipeline, or several lines.

## The language

### Pipelines

`a | b x` evaluates `a`, then calls `b` with the returned value followed by `x`. The value stays one argument, even when it contains spaces, newlines, or pipes.

A command returns text, a list, or no value. No value cannot feed another command. The REPL prints text as is and list items one per line.

```text
cmdr> text "pear,apple,pear" | split , | sort | uniq | join ", "
apple, pear
cmdr> add 2 3 | mul 4 | sub 1
19
```

Syntax and command names are checked for the whole pipeline before any command runs. Effects from earlier stages remain if a later stage fails.

List commands accept lists or newline-separated text; in text, a final newline ends the last item. `sort`, `uniq`, `head`, `tail`, `filter`, and `keep` return text for text input and lists for lists, so `read app.log | filter ERROR | upper` works. `lines`, `split`, `map`, `each`, `list`, and `files` return lists. Text commands reject lists; use `map` per item or `join` first.

### Words and lines

- Whitespace separates arguments.

- Single and double quotes preserve spaces and pipes, including empty arguments (`""`).

- Backslash escapes the next character outside single quotes. Inside double quotes, `\n` and `\t` are a newline and a tab.

- `$name` is a reference. Quote or escape `$` and braces for literal text; `a$x` is literal.

- References and sub-pipelines substitute values, never text to parse again. There is no globbing, word splitting, redirection, or implicit shell.

- A line starting with `#` is a comment, including a `#!/usr/bin/env cmdr` shebang. `#` later in a line is text.

- A line ending in an unescaped `|` continues on the next line. Blank and comment lines inside a continuation are skipped.

- Quotes and blocks cannot span lines otherwise. Write a newline as `\n`, and end a line inside a block with `|` to continue it. A block's first stage starts on the line that opens the block: `{ (s) |` alone is an error.

### Sub-pipelines

`$(PIPELINE)` is an argument whose value is PIPELINE's result.

- It runs when its stage runs, before the command, left to right among the arguments.

- It receives no input.

- Its names mean what they would beside it, and it is checked with its line.

- Its result is one argument, even a list.

```text
cmdr> text notes.txt has $(read notes.txt | lines | count) lines and $(read notes.txt | length) bytes
notes.txt has 3 lines and 14 bytes
```

Each `$(...)` runs again where it appears. When a value is needed twice, name it once with `as` or `with`:

```text
cmdr> read notes.txt | as notes
cmdr> text notes.txt has $($notes | lines | count) lines and $($notes | length) bytes
notes.txt has 3 lines and 14 bytes
```

Keep the main flow of data as stages, and keep effects out of arguments. Unquoted parentheses inside a sub-pipeline balance, so `$(text (a))` keeps its parentheses. A sub-pipeline cannot be a command name.

### Blocks and definitions

```text
cmdr> def wrap { (x d) text $d $x $d }
cmdr> list a b | map wrap - | join ,
- a -,- b -
cmdr> def clean-or { (s fallback) trim $s | or { nonempty | upper } $fallback }
cmdr> text '   ' | clean-or none
none
cmdr> def without { (xs v) keep $xs { not { equals $v } } }
cmdr> list a b c | without b | join ,
a,c
```

`{ ... }` is a block: a pipeline passed as a value. Its syntax, command names, and references are checked with the enclosing line, before any stage runs.

- `{ (a b) ... }` declares parameters, referenced as `$a` and `$b`. A block takes exactly the arguments it declares. `{ ... }` takes one input, received by pipe; `{ () ... }` takes none.

- Declared parameters appear only where named, and each must be used. Only `{ ... }` pipes its input into its first stage, so a block can start from something else: `{ (w) read app.log | contains $w }`.

- `_` declares a parameter the block ignores: `list a b | each { (_) random.random }`. It may repeat, needs no use, and cannot be referenced or have a default. It still takes its argument, so `{ (_ x) ... }` takes two. Use it for an argument the caller passes regardless, not to silence the error about a parameter you meant to use.

- A parameter after the first may have a default: `{ (chain amount=50) ... }`. A call may omit it, and parameters after it need defaults too. A default is one word without spaces, quotes, or `)`, or `""` for empty text.

- Each later stage receives the previous result first. A stage that is a lone `$name` makes that value the result.

- Names are lexical. A nested block sees the names of enclosing blocks, and an inner name shadows an outer one. A block keeps the values it captured.

- A reference substitutes a value: a list stays one argument. Unquoted braces inside a block must balance.

```text
cmdr> def greet { (name greeting=hello) text $greeting $name }
cmdr> greet bob
hello bob
cmdr> greet bob hi
hi bob
```

`each`, `keep`, `not`, `with`, `fold`, `if`, `try`, `or`, `check`, `repeat`, and `retry` take blocks. Extra arguments follow the item: `list a b c | keep { (s v) not $s { equals $v } } b`. `repeat` and `retry` pass only the extra arguments, so a block that needs no counter declares none: `repeat 3 { () text x }`.

**`def NAME BLOCK [DOC]`** defines a command for the rest of the session.

- `help` shows DOC, or the body if DOC is absent. The `help` overview shows defaulted parameters as `...`, and `help NAME` shows each as `[AMOUNT=50]`.

- Definitions are lexical: a body uses the commands that existed when it was defined, and a body that names its own command calls the one it shadows.

- A definition may shadow any command except `def`.

- Errors from a body are prefixed with its name: `outer: inner: equals: ...`. A mismatch from a def called on the line also shows the call's arguments: `ends-with ar bar: ge: 2 is not at least 3`.

**`as VALUE NAME [DOC]`** binds VALUE to NAME. NAME becomes a command of no arguments that returns VALUE, as a Python assignment names a value. Like `def`, `as` returns no value, so it ends its pipeline and prints nothing:

```text
cmdr> text 440 | as hz "Concert A"
cmdr> hz | mul 2
880
```

- A binding follows `def`'s rules. Later lines see NAME; the line that binds it does not.

- A definition keeps the value it saw, and binding NAME again shadows it.

- VALUE is constant and cannot be a block; use `def` for code.

- A module exports a binding as MODULE.NAME.

**`$NAME`** refers to a binding where no enclosing block declares NAME, so a binding can be an argument. A plain NAME there is the word itself:

```text
cmdr> text a.wav | as a
cmdr> text b.wav | as b
cmdr> list $a $b | join ,
a.wav,b.wav
cmdr> list a b | join ,
a,b
```

A block's parameter shadows a binding of the same name. `$MODULE.NAME` refers to a module's binding. `$upper` is an error, since `upper` is a command, not a value.

### Control flow

**Predicates** return their input when they hold, so they compose mid-pipeline: `nonempty`, `equals`, `contains`, `matches`, and `lt`, `gt`, `le`, `ge`. When a predicate does not hold, it fails with a mismatch.

- `keep`, `not`, `if`, `or`, and `check` treat a mismatch as false, and let other failures through as errors.

- `try` and `retry` catch a mismatch like any failure.

- Empty text and `"0"` are not false.

**`if [VALUE] COND THEN [elif COND THEN ...] [else ELSE]`** runs one branch. Each COND runs with VALUE in turn; the THEN of the first that holds runs with VALUE, and if none holds, ELSE does.

- Only the chosen branch runs, so it may fail or have effects that the others must not.

- An error in a COND propagates, unlike `try`.

- Without `else`, the last mismatch is the result, so `if` also works as a guard inside `keep`.

- `elif` and `else` are words that count only after a THEN.

```text
cmdr> text b | if { equals a } { (_) text A } elif { equals b } { (_) text B } else { (_) text C }
B
cmdr> list 3 12 7 | keep { if { gt 5 } { (n) lt $n 10 } }
7
```

**`try [INPUT] BLOCK FALLBACK`** returns the block's result, or FALLBACK if the block fails at run time. Its arity is checked first, so a typo or a missing input is an error, not a fallback. A FALLBACK block is a handler: it receives the error message, as Python's `except E as e` binds it, and its result is `try`'s.

```text
cmdr> list a.txt b.txt | each { try { read } "" }
cmdr> try { () read config.txt } { (e) concat "no config: " $e }
no config: read config.txt: No such file or directory
```

**`or [INPUT] BLOCK FALLBACK`** is `try` for predicates: only a mismatch selects FALLBACK, and errors propagate. **`check [INPUT] BLOCK MESSAGE`** replaces a mismatch's message, so a composed predicate can report `b is not a` instead of the commands it is built from.

**`retry N [every SECONDS] BLOCK [ARG ...]`** calls its block until a call succeeds, at most N times, and returns that result. If all fail, it fails with the last failure, a mismatch kept as one. `every SECONDS` waits between attempts, since a block's stages pipe into each other and cannot wait themselves. Ctrl-C ends the wait.

```text
cmdr> retry 30 every 1 { () run curl -sf http://localhost:8080/health }
```

**`repeat N BLOCK [ARG ...]`** calls a block N times and lists the results. **`with VALUE BLOCK`** names a computed value for a block, `length $x | with { (n) drop $s $n }`, where `$(...)` puts it in place, `drop $s $(length $x)`. Use `with` when the block uses the value more than once.

Choose by what should happen on failure:

| To | Use |
|-|-|
| Choose a branch, letting errors through | `if` |
| Fall back to a cheap value on a mismatch | `or` |
| Recover from any failure | `try` |
| Filter a list | `keep` |
| Replace a mismatch's message | `check` |
| Wait for a condition | `retry` |

### Text and numbers

**`concat [VALUE ...]`** joins its arguments without a separator: text adds itself and a list its items, so `list a b | concat` and `concat "### " $f "\n"` both work. `text` joins with spaces.

**`format TEMPLATE [ARG ...]`** fills a template, as Python's `str.format` without format specifications.

- `{}` takes the next argument, and `{N}` argument N from 0, so a value can appear twice.

- `{{` and `}}` are braces.

- An argument no field uses is an error, where Python ignores it.

```text
cmdr> format "{} has {} lines." notes.txt $(read notes.txt | lines | count)
notes.txt has 3 lines.
cmdr> read greeting.tmpl | format $user
```

Use `format` when the result reads as a sentence, or the template is data, as from a file. Use `concat` to join parts.

Text is bytes in the C locale; `length`, `take`, and `drop` count bytes. Files and program output containing NUL bytes are rejected. Numbers are decimal, such as `-1.5e3`; hex and surrounding spaces are rejected. Results print with the fewest digits that read back exactly: `div 1 3` gives `0.3333333333333333`.

`matches` and the `re` extension pass REGEX to the system's `regcomp`, which bounds neither memory nor time. On glibc, `((a{255}){255}){255}` took 3.5 GB, and the 18-byte `(^|$|\b|\B|\<|\>)*` did not finish in 20 s. Do not take REGEX from untrusted input; search such input as TEXT.

### Programs

**`run PROGRAM [ARG ...]`** resolves PROGRAM through PATH, passes arguments unchanged, and returns stdout after the program exits.

- `run` supplies empty stdin; `run-in TEXT PROGRAM ...` supplies TEXT.

- A nonzero exit status, such as `grep` without matches, fails the stage.

- The program's stderr is not captured; it goes to cmdr's stderr.

```text
cmdr> text "hello world" | run-in tr a-z A-Z
HELLO WORLD
```

**`run-with OPTIONS PROGRAM [ARG ...]`** captures stderr and returns the exit status as data: `STATUS STDOUT STDERR`.

- OPTIONS is a list of NAME VALUE pairs: `dir` sets the working directory, `stdin` the input, `timeout` a limit in seconds, and `group 1` runs the program in its own process group.

- Past the limit the program gets SIGTERM, then SIGKILL a second later. Its own children are signalled only with `group 1`. A program in its own group does not get Ctrl-C from the terminal, and cannot read the terminal.

- STATUS is the exit status, 128 plus the signal number, or `timeout`.

- A list argument adds its items as arguments, at any depth, so an empty list adds none.

- `dir` needs `posix_spawn_file_actions_addchdir`, or its older `_np` form (glibc 2.29, macOS 10.15); without either, `dir` is an error.

```text
cmdr> list dir / | run-with sh -c 'pwd; echo warn >&2; exit 3' | each { trim } | join ,
3,/,warn
cmdr> list | run-with printf '%s ' $(list --model opus) x | nth 2
--model opus x
```

**`run-all JOBS N`** runs programs in parallel, at most N at once, and returns each job's `STATUS STDOUT STDERR` in the order of JOBS.

- Each job is a list `OPTIONS PROGRAM [ARG ...]`, as `run-with` takes. Each job's timeout runs from its own start.
- N is a whole number from 1 to 64.
- A job that cannot start fails the stage. The running jobs then get SIGTERM, then SIGKILL a second later, as on Ctrl-C.

```text
cmdr> list a b c | each { (d) list $(list dir $d timeout 3600 group 1) aw.cmdr audit . } | run-all 3 | each { first } | join ,
0,0,timeout
```

### Scripts

`cmdr FILE [ARG ...]` runs FILE and stops at its first failing line, reporting `FILE:LINE:`.

- `script-args` returns the ARGs as a list, like Python's `sys.argv[1:]`.

- `script-file` returns the absolute path of the script file in which it is written, like `realpath(__file__)`. `script-dir` returns that file's directory.

- A `def` in `lib/x.cmdr` whose body uses `script-dir` returns `lib`, even when a script elsewhere calls it.

- In the REPL, `-c`, and built-in modules, all three are errors.

`load PATH [ARG ...]` runs a script in the current session, so its definitions remain, and passes it ARGs. A relative PATH resolves from the current directory, as in a shell. To load a file next to the script, write `script-dir | path-join lib.cmdr | load`.

### Modules

`import NAME [as ALIAS]` follows Python's `import`. The module runs once per session, with no arguments and without printing its values. The importer gets `NAME.DEF` for each definition, or `ALIAS.DEF` with `as`. The module's other names, including its own imports, stay inside it.

```text
cmdr> import num as n
cmdr> n.seq 1 3 | join ,
1,2,3
```

`import` looks for `NAME/__init__.cmdr`, then `NAME.cmdr`, in each of:

1. the directory of the file in which `import` is written, or the current directory in the REPL and `-c`;

2. each directory in `CMDR_PATH`, separated by `:`;

3. `~/.cmdr/lib`;

4. `share/cmdr/lib` under the install prefix.

A package finds its own files with `script-dir`.

**`from NAME import DEF [as ALIAS] ...`** imports the module the same way, then binds only the DEFs listed, as `DEF` or `ALIAS`.

- A name that already exists is an error, and then the statement binds nothing. That includes a primitive, a prelude definition, or an earlier `def`, since binding over it would change what later lines call without warning.

- `as` gives the definition another name; `DEF as DEF` shadows the existing one on purpose.

- Binding the same definition to the same name again is allowed, and a later `def` may still shadow it.

- There is no `from NAME import *`.

```text
cmdr> from re import split
cmdr: from re import split: split already exists; bind it with as
cmdr> from re import split as re-split
cmdr> re-split a1b2c [0-9] | join ,
a,b,c
```

Inside a module, `from` binds names the module exports, as in Python; `import` binds names it keeps.

**Built-in modules.** The extensions in `ext/` are modules, imported by name: `import fs` gives `fs.exists` and the rest. Nothing of an extension is visible before `import`, as in Python.

- Each extension is two modules. Its `.cmdr` module, which users import, re-exports the C commands from `_fs` with any definitions of its own: after `import env`, `env.get` is a C command and `env.or` a definition.

- `_fs` is private to `fs` by convention, as Python's `_socket` is to `socket`. cmdr does not enforce this.

- Built-in modules are found before files, so a local `random.cmdr` cannot hide the built-in `random`; Python lets a local `random.py` do that.

| Module | Commands |
|-|-|
| [env](ext/env/README.md) | Environment variables |
| [fs](ext/fs/README.md) | Path tests, sizes, times, `fs.mkdir`, `fs.remove`, `fs.move` |
| [num](ext/num/README.md) | `num.seq`, modulo, rounding |
| [random](ext/random/README.md) | Seeded random numbers, choices, shuffles, and samples, as Python's `random` |
| [re](ext/re/README.md) | Regex extraction, splitting, and replacement |
| [shlex](ext/shlex/README.md) | Shell-style word splitting and quoting, as Python's `shlex` |
| [str](ext/str/README.md) | Byte offsets, repetition, padding, reversal |
| [time](ext/time/README.md) | The current time, and strftime formatting |

After `import NAME`, `help NAME.DEF` shows a command's usage and docstring.

### Commands

These are the primitives, in [src/primitives.c](src/primitives.c). The prelude defines more, such as `first`, `nth`, `sum`, `words`, and `path-join`; `help` lists every command.

| Commands | Behavior |
| --- | --- |
| `text [ARG ...]` | Join arguments with spaces |
| `concat [VALUE ...]` | Join text, and list items, without a separator |
| `format TEMPLATE [ARG ...]` | Fill `{}` and `{N}` in TEMPLATE with ARGs, as Python's `str.format` |
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
| `map VALUE COMMAND [ARG ...]` | Call a command per item; COMMAND is looked up when `map` runs |
| `each VALUE BLOCK [ARG ...]` | Call a block per item |
| `keep VALUE BLOCK [ARG ...]` | Keep items whose block holds |
| `not VALUE BLOCK [ARG ...]` | Return VALUE if the block mismatches |
| `with VALUE BLOCK [ARG ...]` | Call a block with VALUE |
| `fold VALUE INIT BLOCK [ARG ...]` | Combine items left to right; the block takes the result so far and the item |
| `repeat N BLOCK [ARG ...]` | Call a block N times with only ARGs; list the results |
| `retry N [every SECONDS] BLOCK [ARG ...]` | Call a block until it succeeds, at most N times |
| `if [VALUE] COND THEN [elif COND THEN ...] [else ELSE]` | Run the THEN of the first COND that holds, else ELSE |
| `try [INPUT] BLOCK FALLBACK` | Return FALLBACK if the block fails, or call a FALLBACK block with the error |
| `or [INPUT] BLOCK FALLBACK` | Return FALLBACK if the block mismatches |
| `check [INPUT] BLOCK MESSAGE` | Mismatch with MESSAGE if the block mismatches |
| `nonempty VALUE`, `equals TEXT X`, `contains TEXT X`, `matches TEXT REGEX`, `lt`/`gt`/`le`/`ge NUMBER X` | Predicates; REGEX is POSIX extended |
| `read PATH`, `write VALUE PATH`, `append VALUE PATH` | Files; write and append return VALUE and store lists one item per line |
| `files [DIR]`, `path-basename PATH`, `path-dirname PATH` | Regular files in DIR, sorted; lexical path parts |
| `run PROGRAM [ARG ...]`, `run-in TEXT PROGRAM [ARG ...]` | Run a program, returning stdout |
| `run-with OPTIONS PROGRAM [ARG ...]` | Run a program with options, returning `STATUS STDOUT STDERR` |
| `run-all JOBS N` | Run jobs, each `OPTIONS PROGRAM [ARG ...]`, N at once; return each one's `STATUS STDOUT STDERR` |
| `load PATH [ARG ...]` | Run a script in this session; PATH is relative to the cwd |
| `import NAME [as ALIAS]` | Run a module once; bind its definitions as `NAME.DEF` |
| `from NAME import DEF [as ALIAS] ...` | Run a module once; bind each DEF unqualified, if the name is new |

A session also provides `help`, `def`, `as`, `script-file`, `script-dir`, and `script-args`.

### Growing a prelude

Start with a pipeline. This one lists the distinct file extensions in the current directory:

```text
cmdr> files | map path-basename | keep { drop 1 | contains . } | each { split . | tail 1 | join "" } | sort | uniq
md
txt
```

`keep { drop 1 | contains . }` keeps names with a dot after the first byte, so it skips `Makefile` and `.gitignore`. `tail 1 | join ""` takes the last item.

[src/prelude.cmdr](src/prelude.cmdr) names the parts, each built on the ones before. These are three of its definitions:

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

Put definitions in a file, such as `ext.cmdr`, and run `load ext.cmdr` in a session or at the top of a script. Because definitions are lexical, a later `def tail` does not change the prelude's `last`.

## Using cmdr

### The REPL

`help` lists the commands. It returns text, so `help | filter map` works, and `help NAME` shows one command's full usage and documentation.

- The prompt is light green, except on terminals without escape sequences (`dumb`, `cons25`, `emacs`). The first line names the version and prelude, such as `cmdr 0.2.0 (prelude: standard)`.

- Highlighting colors commands, references, blocks, sub-pipelines, and strings. Names bound by `as` are bold magenta, where bound and where used. Unknown commands are red.

- Tab completes command names. Up and Down recall history, saved in `~/.cmdr_history`, with a continued pipeline stored as one entry.

- Ctrl-C drops the current pipeline. Pasted lines run one at a time, each echoed after its prompt.

- Line editing uses a vendored [linenoise](thirdparty/linenoise). Configure with `-DCMDR_EDITOR=OFF` to read plain lines instead.

Syntax highlighting for Vim, Sublime Text, VS Code, Helix, and Zed is in [editors/](editors/README.md).

### Command line

| Command | Runs |
|-|-|
| `cmdr` | The REPL, or one pipeline per line from piped stdin, continuing after errors |
| `cmdr FILE [ARG ...]` | A script, stopping at its first failing line |
| `cmdr -c 'PIPELINE'` | PIPELINE, or several lines in turn, stopping at the first failure: `cmdr -c $'import fs\nfs.exists /tmp'`. A newline inside quotes or a block stays in its pipeline |
| `cmdr --user ...` | Any of the above with `~/.cmdr/prelude.cmdr` in place of the standard prelude; an error if the file is missing |
| `cmdr --version` | Prints the version |

Errors go to stderr, and the exit status is nonzero if any line failed. Ctrl-C while a line runs stops it, and cmdr exits by SIGINT, so a calling shell stops too. A running program gets SIGTERM, then SIGKILL a second later, so one that ignores SIGINT does not outlive cmdr. SIGTERM to cmdr does the same, and cmdr exits by SIGTERM.

## Extending cmdr

Write a command in C only when it cannot be composed. For the `cmdr` executable, write an extension; to use cmdr inside another program, embed it.

### Extensions

An extension adds C commands to the `cmdr` executable without editing `src/main.c`. Each is a directory `ext/NAME/`:

- `*.c`: the commands, the module `_NAME`. One file defines `const cmdr cmdr_ext_NAME`, with `-` in NAME as `_`.

- `NAME.cmdr`: the module `NAME`, which users import. The build embeds it, and it runs at its first `import`. Other `.cmdr` files are modules named after their files.

- `README.md`: what the extension does. The build fails without it.

- `tests/`: at least one `*.cmdr` or `*.c` file; configure fails without one. `make test` runs each `.cmdr` file with `cmdr`, in `tests/`, and fails at its first failing line. Each `.c` file is a program linked with the extension's sources and `libcmdr.a`, for checks a script cannot make, such as error messages. Configuring with `-DCMDR_BUILD_TESTS=OFF` skips both.

The C commands have plain names, such as `exists`. `NAME.cmdr` chooses what `import NAME` gives, as `ext/fs/fs.cmdr`'s one line does:

```text
from _fs import exists is-file is-dir dirs size mtime mkdir remove move
```

[ext/env](ext/env/README.md) is an example. Its C command `get` becomes `env.get`, since `ext/env/env.cmdr` re-exports it:

```c
static const cmdr_command commands[] = {
    {.name = "get", .usage = "get NAME", .docstring = "Return an environment variable",
     .run = env_command, .text = 1},
};
const cmdr cmdr_ext_env = {commands, sizeof(commands) / sizeof(commands[0]), NULL};
```

- A C table that defines a name twice fails at `import`. Other names cannot collide: an extension's names are `NAME.DEF`, beside the primitives.

- A module runs at its first `import`, after the prelude. Extension modules use only primitives and other modules, since `--user` may replace the prelude.

- Extension C code can use `cmdr_number`, `cmdr_number_value`, and `cmdr_natural` to parse and print numbers as primitives do. Test programs include `tests/ext_check.h` for `CHECK`, `expect`, and `expect_error`.

`scripts/mk_ext.py NAME` creates `ext/NAME/` with these files and a command `NAME.echo TEXT`, which returns TEXT. The skeleton builds and passes its tests, so edit from there. `--c-test` also creates `tests/test_NAME.c`. It refuses an existing directory. `make test` builds a fresh skeleton, so a template change that breaks it fails there.

### Embedding

The build produces `libcmdr.a`, with the API in [include/cmdr.h](include/cmdr.h). A host puts its commands in a table in front of the primitives in [src/primitives.c](src/primitives.c), where they shadow them, and opens a session on it:

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

**Commands.**

- `.text = 1` guarantees text arguments, and `.user` is passed to `.run`.

- A command with `.value` in place of `.run` is a constant, as `as` makes: calling it returns the value, and `$NAME` refers to it.

- Arguments are borrowed, including the values a block captured; clone what you keep. The result is owned by the caller.

- A predicate returns `CMDR_MISMATCH` when it does not hold; `cmdr_describe` gives a short form of a value for its message.

**Sessions.**

- A session adds `help`, `def`, `as`, `script-file`, `script-dir`, and `script-args`, and frees its definitions on `cmdr_session_close`. Sessions on different threads are independent.

- `cmdr_session_load` runs a script and stops at its first failure.

- The second argument of `cmdr_session_open` allows effects. With it, the session adds `load`, `import`, and `from`, and `cmdr_session_prelude` also runs [src/prelude-effects.cmdr](src/prelude-effects.cmdr), which defines `cat`, `grep`, `sh`, and `sh-in`.

- For a console without file or process access, pass 0 and end the host's table in `&cmdr_pure`, which omits `read`, `write`, `append`, `files`, `run`, and `run-in`.

- `cmdr_scan` splits source into tokens with the parser's rules, tolerating unfinished input, for a host's highlighting or completion. The REPL highlights with it.

- `cmdr_session_add_module` makes a C table importable as a module, and `cmdr_session_add_source` makes a script's source one. They are found before files. Extensions are part of the executable, not `libcmdr`, so a host adds its own this way.

- [src/main.c](src/main.c) is the cmdr executable, built on this API.

**The process.**

- cmdr parses and prints numbers with `.` only while `LC_NUMERIC` is `"C"`, the default. A host that calls `setlocale` must leave that category alone.

- Allocation failure exits the process with status 1. `cmdr_set_out_of_memory` installs a handler that runs first, to log or save host state.

- `cmdr_interrupt(1)`, from a SIGINT handler, makes every later command call fail with `interrupted`, which `try` does not catch, and stops a program that `run` is waiting for. The flag is process-wide; the host clears it with `cmdr_interrupt(0)`.

### The standard prelude

Move a definition into [src/prelude.cmdr](src/prelude.cmdr) when every session should have it, or into [src/prelude-effects.cmdr](src/prelude-effects.cmdr) if it reads files or runs programs. The build embeds both files, and cmdr runs them at startup; an error there stops cmdr with `prelude.cmdr:LINE:`.

## Install

```sh
cmake -S . -B build && cmake --build build
cmake --install build --prefix /usr/local
```

This installs `cmdr`, `libcmdr.a`, `libcmdr.so`, `cmdr.h`, a CMake package, and `cmdr.pc`.

- A CMake host uses `find_package(cmdr)` and links `cmdr::static` or `cmdr::shared`; others use `pkg-config --cflags --libs cmdr`.

- `-DCMDR_SHARED=OFF` builds only the static library.

- The shared library's soname is `libcmdr.so.0.2`, since a minor version may break the API before 1.0.

The build needs POSIX-2008 (`getline`, `open_memstream`, `posix_spawn`, `fstatat`).

## Checks

`make test` builds cmdr and runs [tests/test_cmdr.py](tests/test_cmdr.py) with Python 3, in parallel.

- It also runs each extension's `tests/`, and [tests/test_api.c](tests/test_api.c) against both libraries.

- It installs into `build/staged` and builds [tests/consumer](tests/consumer) against it.

- Each test method is a CTest test, so `ctest --test-dir build -R test_editor` runs only the editor tests. Tests that need the editor show as skipped in a build without it.

- Configure with `-DCMDR_BUILD_TESTS=OFF` to build without Python.

`make` also builds the binaries of [examples/sox-dsl](examples/sox-dsl/README.md) and [examples/midi](examples/midi/README.md), `cmdr-snd` and `cmdr-midi`, and `make test` runs their tests. Each skips itself if its libraries or compiler are missing. Configure with `-DCMDR_EXAMPLES=OFF` to skip both. `cmdr` itself never includes them.
