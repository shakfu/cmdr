# Changelog

## Unreleased

### Fixed

- In a module built into the executable, such as an extension's, `script-file`, `script-dir`, and `script-args` returned the importing script's, since the module's definitions compiled where the importer's script was in scope. They now fail, as in the REPL: the module has no file.

## [0.2.0]

### Changed

- `path-ext` takes the basename first and mismatches when it has no extension. `Makefile` gave `Makefile`, `.bashrc` gave `bashrc`, and `a.b/c` gave `b/c`. A mismatch over Python's empty text, so `or` supplies a default and `path-ext` agrees with `path-has-ext`.

- Numbers print with the fewest digits, up to 17, that read back as the same double: `div 1 3` gives `0.3333333333333333`, not `0.33333333333333331`.

- Numbers must be decimal. `strtod` also accepted hex such as `0x10` and leading spaces. Hosts must keep `LC_NUMERIC` as `"C"`; documenting this over a per-call `uselocale`, which costs a locale switch per number.

- Path commands share the `path-` prefix: `basename`, `dirname`, `ext`, `has-ext`, and `stem` are now `path-basename`, `path-dirname`, `path-ext`, `path-has-ext`, and `path-stem`. The prefix groups them, as `run-` and `script-` do.

- **Blocks replace string bodies and positional `$N`.** `{ ... }` is a pipeline passed as a value. Its command names and references are checked with the enclosing line, before any stage runs. Blocks declare parameters, which resolve lexically through nested blocks. Lexical scope over per-call `$N`, because per-call scope passed each outer value down at every nesting level and could not return a closure. See [docs/dev/blocks.md](docs/dev/blocks.md).

  ```text
  def wrap { (x d) text $d $x $d }
  def without { (xs v) keep $xs { not { equals $v } } }
  list a.txt b.txt | each { try { read } "" }
  ```

  Declared parameters appear only where named, and each must be used. Only `{ ... }` pipes its input into its first stage, and block arity is exact: `{ () ... }` takes no input. Before, naming a value anywhere in a stage stopped it being piped into that stage. So `try { add 1 } $1`, meant as "fallback to the item", ran its body without input and returned the fallback with no error. It now fails with `try: block takes 1 argument, got 0`.

  Breaking: `def NAME 'PIPELINE'`, `$1..$N`, and string bodies for `each`, `keep`, `not`, and `try` are now errors. `cmdr_check` is removed, and `cmdr_eval` no longer takes arguments.

- Composed commands are `def`s in `src/prelude.cmdr`, embedded at build time and run at startup. The `.pipeline` field of `cmdr_command` is removed. `src/prelude.c` is now `src/primitives.c`, and `cmdr_prelude` is now `cmdr_primitives`.

- `help` lists built-in commands, then primitives, then definitions oldest first. It listed definitions first, newest first.

- A block resolves command names where it was written, and `cmdr_run` no longer takes a registry. `cmdr_command.scope` is removed: a block's compiled code carries its scope. See Fixed.

- `last` fails with a mismatch on an empty list or text; it returned empty text, which could not be told apart from an empty last item. `first` does the same, and so does `path-ext` on empty text.

- Mismatch messages name the values involved: `equals: foo differs from bar`, `not: [1] satisfies { nonempty }`. They omitted the tested value, so a composed predicate's failure did not say which input failed. A mismatch from a def called on a line also shows the call's arguments, `ends-with ar bar: ...`; errors keep the plain call chain, which serves debugging. Values print in one line, up to 32 bytes and 5 items.

- Each test method is a CTest test, read from `tests/test_cmdr.py` at configure time, and `make test` runs them in parallel.

- The build is split into three files: `CMakeLists.txt`, `cmake/CmdrExtensions.cmake` for code generation and `cmdr_add_binary`, and `tests/CMakeLists.txt`. The test fixture binaries now build in `build/tests/`.

- `docs/dev/scenarios-of-use.md` marks regex capture and environment access done, through `ext/re` and `ext/env`, and shows D1 log triage with `re.groups`.

### Added

- **`help NAME`** shows one command's full usage and documentation. The `help` overview shows a definition's defaulted parameters as `...`, as `snd.reverb CHAIN ...`, so a long signature does not widen every row. This lets parameters keep descriptive names, such as sox's `hf-damping`. Only definitions are shortened: their usage is generated, with defaults last, while a C command's usage is written by hand and may bracket a leading argument, as `try [INPUT] BLOCK FALLBACK` does.

- **Default parameter values.** A parameter after the first may declare a literal default, `{ (chain amount=50) ... }`, and a call may omit it, so `def reverb { (chain amount=50) ... }` accepts `reverb` and `reverb 80`. Defaults trail the required parameters, so missing a required argument is still an error, and E2 in [docs/dev/blocks.md](docs/dev/blocks.md) still fails. The first parameter cannot have one, since a block could then run without its input. Literal defaults over keyword arguments, because `NAME=VALUE` words such as `user.name=demo` are ordinary arguments today. `help NAME` shows `[AMOUNT=50]`. `cmdr_block_arity` accepts a range, and `cmdr_run` fills omitted parameters. See [docs/dev/defaults.md](docs/dev/defaults.md).

- **`from NAME import DEF [as ALIAS] ...`** binds the DEFs listed from a module without the prefix. The module runs once, shared with `import`. A name that already exists fails the whole statement unless it is the same definition, because shadowing a primitive or prelude name changes what later lines call without warning. `from snd import trim` fails, since `trim` is a primitive, while `from snd import trim as cut` works. Python's `from` shadows silently. There is no `*`, because modules have no export list. See [docs/dev/import.md](docs/dev/import.md).

- **`import NAME [as ALIAS]`** runs a module once per session and binds its definitions as `NAME.DEF`, as Python's `import` does. It searches the importing file's directory, `CMDR_PATH`, `~/.cmdr/lib`, and `share/cmdr/lib`; a package `NAME/__init__.cmdr` wins over `NAME.cmdr`. The module's other names stay inside it. Qualified names over a required `NAME-` prefix, because moving every module and importer to qualified names later would cost more with each module written. See [docs/dev/import.md](docs/dev/import.md).

- **`run-with OPTIONS PROGRAM [ARG ...]`** runs a program with `dir`, `stdin`, and `timeout` options. It returns `STATUS STDOUT STDERR`, where `run` fails on a nonzero status. List arguments are flattened, so an optional flag is an empty or two-item list. One command with options over a `run` variant per option, since the options combine. See [docs/dev/run-with-script-push.md](docs/dev/run-with-script-push.md).

- **`script-file` and `script-dir`** return the absolute path and directory of the script file in which they are written, like `realpath(__file__)` in Python, so a script can find its own files. A `def` in `lib/x.cmdr` whose body uses `script-dir` returns `lib`, even when a script elsewhere calls it. The alternative, the running script, would give that `def` its caller's directory.

- **Script arguments.** `cmdr FILE [ARG ...]` and `load PATH [ARG ...]` pass arguments, which `script-args` returns as a list. It resolves as `script-file` does, so a loaded library sees its own arguments, not the top script's. A process-wide list, like Python's `sys.argv`, would let a library depend on its caller's command line. `cmdr_session_load_args` passes arguments from a host. See [docs/dev/run-with-script-push.md](docs/dev/run-with-script-push.md#script-args).

- Extension `shlex`: `shlex.split`, `shlex.quote`, and `shlex.join`, as Python's `shlex` with `posix=True`. `shlex.split "git commit -m 'two words'"` gives four words, which `split` cannot, and nothing is expanded or run. See [ext/shlex](ext/shlex/README.md).

- `path-join BASE PATH` joins two paths with one `/`, as Python's `os.path.join` does; an absolute PATH replaces BASE. `path-split PATH` returns `path-dirname` and `path-basename` of PATH, like `os.path.split` but with POSIX rules: `path-split c` gives `.` and `c`. Both are prelude `def`s, so `script-dir | path-join lib.cmdr | load` needs no `concat`.

- `push LIST [ITEM ...]` appends items to a list. `list $xs x` nests `$xs` as one item, so `fold` could not collect results.

- `examples/agent-workflows`: chains of coding agents (claude, codex, cursor, pi, opencode) over a project, bug, or feature target. It is the package `aw`, used with `import aw`; `aw.cmdr` is its command-line entry point. `demo.cmdr` fixes a reported bug in a sample project with one agent. CTest runs the demo, `aw.cmdr`, and a test of every entry point with a dry agent. The `llama` agent sends the tracked files to llama-server and applies the edit blocks in its reply, retrying once with the reasons any failed; it runs no commands, so it needs no sandbox. The `claude` adapter fails before it starts if `bwrap` cannot create a network namespace, as under Ubuntu's `apparmor_restrict_unprivileged_userns`. Claude Code's `failIfUnavailable` did not catch this, and every Bash call failed while the step exited 0.

- `examples/sox-dsl`: sox in cmdr. The `snd` extension runs sox's arguments in-process through libsox: all 62 effects, the six `--combine` methods, format options, the null file, and the default audio device for play and record. It adds `varispeed`, which sox lacks: speed changes linearly over the output, on libsoxr's variable-rate mode. The `snd` module builds chains as lists of those arguments and splits files at times, at silences, and by channel. libsox over libsoxr with effects written here, because it covers sox's effects with sox's own arguments and DSP. Configure builds `cmdr-snd` only when pkg-config finds libsox and libsoxr, so `cmdr` depends on neither. See [examples/sox-dsl](examples/sox-dsl/README.md).

- `examples/midi`: MIDI generation in cmdr, on the vendored libremidi 5.4.3. The `midi` extension plays scores on MIDI output ports, reads and writes Standard MIDI Files, and makes Euclidean rhythms by Bjorklund's algorithm. The `midi` module builds scores from scales, chords, scale degrees, and rhythms; draws them by chance with `pick`, `walk`, `chance`, and `humanize`; and transposes, shifts, stretches, stacks, sequences, and loops them. A score is a list of items as text, `KIND START ...` in beats: notes, rests, controllers, programs, pitch bends, and tempo changes. Text over handles held in C, so scores print and pass through list commands. Rests count toward a score's length, so joining, looping, and files keep a trailing gap. Configure builds `cmdr-midi` only with a C++ compiler and CMake 3.22, so `cmdr` needs neither. See [examples/midi](examples/midi/README.md).

- Extension `random`: `random.seed`, `random`, `random.int`, `random.uniform`, `random.gauss`, `random.choice`, `random.choices`, `random.shuffle`, and `random.sample`, as Python's `random`. It uses Python's Mersenne Twister and seeding, so after `random.seed N` each command returns what Python returns after `random.seed(N)`. Python's generator over a smaller one, so a seed means the same in both and results can be checked against Python.

- `-DCMDR_EXAMPLES=OFF` skips building `cmdr-snd` and `cmdr-midi` and their tests. It defaults to `ON`, so `make test` still runs the examples' tests.

- The Vim, Sublime, and VS Code grammars in `editors/` mark `$(...)`: `$(` and its balancing `)` as punctuation, and the first word inside as a command. They also mark `_` parameters and `$MODULE.NAME` references.

- `concat [VALUE ...]` joins its arguments without a separator, a list contributing its items; it was a prelude definition over one list. Text was built as `list ... | concat` 69 times in the repository's scripts; `concat "### " $f` needs no `list`. Variadic `concat` over a `format` command, which would add a placeholder language. Text input is no longer split into lines first, so its newlines stay.

- `format TEMPLATE [ARG ...]` fills `{}` and `{N}` in TEMPLATE, as Python's `str.format` without format specifications; `{{` and `}}` are braces. `concat` cannot put text directly after a `$(...)`, and its quoted pieces hide a sentence's shape; `format` keeps the template whole, and a template can be read from a file. An argument no field uses is an error, where Python ignores it, as cmdr rejects a parameter no block uses.

- `try`'s FALLBACK, when a block, is called with the error message, as Python's `except E as e`. Before, the reason was lost: `aw`'s edit-stage replaced every parse failure with one message, and now names the part that was missing. Breaking: a block as FALLBACK was returned as a value; `or` still returns one so.

- `retry N [every SECONDS] BLOCK [ARG ...]` calls a block until it succeeds, at most N times, waiting SECONDS between attempts. A `def` cannot recurse and `repeat` runs a fixed count, so polling and retries could not be written. A bounded retry over `while COND`, since cmdr values carry no state between iterations; the wait is part of `retry`, since a block's stages pipe into each other.

- `$(PIPELINE)` is an argument whose value is PIPELINE's result: `text has $(read f | lines | count) lines`. An argument could only be a literal, a `$name`, or a block, so each computed argument needed a `with` to name it first; 372 of the 430 `with` blocks in the repository's scripts existed only for that. It runs before its stage's command, left to right, with no input, and is checked with its line. `$(...)` over `(...)`, which would change the meaning of existing text such as `(a)`. See [docs/dev/missing-syntax.md](docs/dev/missing-syntax.md).

- `if [VALUE] COND THEN [elif COND THEN ...] [else ELSE]` runs the THEN of the first COND that holds, else ELSE, each with VALUE. Only the chosen branch runs. `or`, `try`, and `check` compute their fallback first, so a branch that fails or has effects needed workarounds: `midi.seq` substituted `0` for a rest's key so the unused note would not fail. An error in a COND propagates, unlike `try`. `else` and `elif` are words, as `as` is in `import`, over a positional fourth block, which reads poorly beside three others and allows no third branch. See [docs/dev/missing-syntax.md](docs/dev/missing-syntax.md).

- `_` in a parameter list declares a parameter the block ignores, exempt from the unused-parameter rule: `each { (_) random.random }`. Callers such as `each`, `fold`, and `with` pass arguments a block may not need, and the rule forced it to use them anyway. A name over relaxing the rule, which catches blocks that ignore their input by mistake. `_` may repeat; `$_` is an error, `nothing is bound to _`. See [docs/dev/missing-syntax.md](docs/dev/missing-syntax.md).

- `repeat N BLOCK [ARG ...]` calls a block N times with only ARGs and lists the results. A block must use each parameter it declares, so `num.range 8 | each { (i) random }` was an error, and repeated calls needed a counter. A count over 10,000,000 is an error, as in `num.seq`.

- `as VALUE NAME [DOC]` binds a value to a name for later lines: `snd.out $c a440.wav | as a440`. NAME is a command returning the value, with `def`'s rules: lexical, shadowable, and exported from modules. Like `def` and Python's assignment, `as` returns no value, so a binding prints nothing in a script. The REPL colors bound names bold magenta, a bold form of the reference color, since a binding is a named value. A constant over a variable, so a definition keeps the value it saw. `as` over `let`, because cmdr passes the value first: `as VALUE NAME` reads as Python's `with ... as name`, and `let VALUE NAME` reverses `let`'s usual order.

- `$NAME` refers to an `as` binding where no enclosing block declares NAME: `list $a $b`. Before, a binding could only start a stage, and `list a b` silently built the words `a` and `b`. It resolves where its line compiles, as command names do, so a definition keeps the value it saw and errors precede effects; resolving at run time would have given dynamic scope. `$NAME` names values only: `$upper` is an error, so a reference never runs code. `cmdr_command` gains `value`, a constant that calling returns and `$NAME` refers to; a host can declare its own. See [docs/dev/bindings.md](docs/dev/bindings.md).

- **Extensions in `ext/NAME/`.** Each holds C commands, `.cmdr` definitions, a `README.md`, and `tests/`, and the build compiles them into `cmdr`. `make test` runs each `tests/*.cmdr` with `cmdr` and builds each `tests/*.c` against the extension. `help` shows C commands with `ext/NAME:` before the docstring. An extension command that reuses an existing name stops startup. A shadowed name would silently change prelude definitions, which bind names when defined. Extension scripts run before the prelude, because `--user` replaces it. See [README.md](README.md#extensions) and [ext/env](ext/env/README.md).

- **A user imports an extension's `.cmdr` module, which imports its C commands.** `import fs` runs `ext/fs/fs.cmdr`, which imports the C commands from `_fs`, private to it, and re-exports them with `from`, with any definitions of its own. `import fs` gives `fs.exists`; before it, `fs.exists` is an unknown command. Before, extension commands were always loaded and named `NAME-DEF`, beside modules' `NAME.DEF`, and only modules supported `from`. Python's `_socket` under `socket`, over keeping extensions always loaded: one rule for every name, and an extension's names cannot collide with others, so the startup collision check is gone; a C table that defines a name twice fails at `import`. The cost is an `import` line per script. Breaking: the bare commands `env` and `random` are `env.get` and `random.random`, and the midi C command `euclid` is `bjorklund`.

  Supporting changes:

  - `from` inside a module binds names the module exports, as in Python. `from M import f as f` shadows an existing `f`, which a module needs to re-export `_re`'s `split`; without `as`, an existing name is still an error.
  - A host's built-in modules are found before files, so a local `random.cmdr` cannot hide `random`. `cmdr_session_add_module` and `cmdr_session_add_source` add them.
  - `-c` runs several lines, as a script does, so `cmdr -c $'import fs\nfs.exists /tmp'` works. A newline inside quotes or a block stays in its pipeline, as before.
  - `examples/sox-dsl` and `examples/midi` keep their modules in `ext/snd/snd.cmdr` and `ext/midi/midi.cmdr`, built in, so they need no `CMDR_PATH`. `midi.play` and `midi.write` take BPM 120 by default.

- Extensions `re`, `num`, `fs`, `time`, and `str`. Each covers a gap a `def` cannot fill: `matches` returns no captures, a `def` cannot loop N times, and no primitive calls `stat`. Every command starts with its extension's name, so a later primitive cannot collide with it. `time.local` and `time.utc` take `[EPOCH] FORMAT` over `FORMAT [EPOCH]`, so a piped time is formatted: `fs.mtime f | time.local %F`. See [README.md](README.md#extensions).

- `cmdr_number`, `cmdr_number_value`, and `cmdr_natural` parse and print numbers as primitives do, for extension code.

- `tests/ext_check.h` gives extension test programs `CHECK`, `expect`, and `expect_error`.

- `scripts/mk_ext.py NAME` creates a skeleton extension that builds and passes its tests. `--c-test` adds a C test program.

- `cmdr_session_source` runs a script held in memory, as `cmdr_session_load` runs one from a file.

- `libcmdr.a` and an embedding API in `include/cmdr.h`, restoring what [docs/dev/minimal.md](docs/dev/minimal.md) removed. `help`, `def`, `load`, and the prelude moved from `src/main.c` into `cmdr_session`, since the prelude is `def`s and a host cannot use it without them. `cmdr_command.user` is passed to `cmdr_fn` as a new second parameter. A session takes the host's table, so a host can omit the primitives. See [docs/dev/scenarios-of-use.md](docs/dev/scenarios-of-use.md).

  The composed-command depth counter is per thread. It was one `static`, so two threads nesting at once shared the limit of 128 and failed with `nested too deeply`.

- `cmdr_pure`, the primitives without `read`, `write`, `append`, `files`, `run`, and `run-in`, for a host console without file or process access. `cmdr_session_open` takes an `effects` flag, which adds `load` and runs `src/prelude-effects.cmdr` after the prelude. The definitions that use effects (`cat`, `grep`, `sh`, `sh-in`) moved there from `src/prelude.cmdr`, since a pure session fails to compile them. `path-basename` and `path-dirname` are pure and now list before `read` in `help`.

- `cmdr_set_out_of_memory` sets a handler that runs before an allocation failure exits. The handler cannot recover by `longjmp`, because the failed evaluation's values would leak, and those values are often what used up the memory. Returning an error from every allocation is the alternative, and [docs/dev/minimal.md](docs/dev/minimal.md) removed it.

- `libcmdr.so` (`-DCMDR_SHARED=OFF` omits it) and install rules: the executable, both libraries, `cmdr.h`, a CMake package (`cmdr::static`, `cmdr::shared`), and `cmdr.pc`. `cmdr.pc` finds its prefix from its own location, so `cmake --install --prefix` works without reconfiguring.

- `editors/`: highlighting for Vim, Sublime Text, and VS Code, and Helix file detection. Helix and Zed need a tree-sitter grammar; see `TODO.md`.

- Primitives `with`, `fold`, `or`, `check`, `matches`, `sort-n`, `le`, and `ge`. `with` passes a computed value to any argument position, which a pipe cannot. `or` is `try` restricted to mismatches, so a conditional does not hide errors as `try` does. `check` replaces a mismatch's message; errors pass through. `matches` uses POSIX extended regular expressions. `ge` and `le` are primitives rather than `not { lt }`, so a failure reads `ge: 4 is not at least 5`.

  ```text
  text foobar | with { (s) length foo | with { (n) drop $s $n } }
  list 3 10 2 | fold 0 { (a x) or $a { gt $x } $x }
  ```

- The prelude defines list commands (`first`, `nth`, `init`, `rest`, `concat`, `unlines`, `unwords`, `distinct`, `reject`, `without`, `compact`, `prefix`, `suffix`, `empty`, `any`, `all`), numbers (`sum`, `product`, `max`, `min`, `mean`, `between`), text (`words`, `starts-with`, `ends-with`, `strip-prefix`, `strip-suffix`), and paths and programs (`path-stem`, `cat`, `grep`, `sh`, `sh-in`). Each is composed from primitives.

- Version 0.1.0, set in `CMakeLists.txt`. `cmdr --version` prints it, and the REPL starts with `cmdr 0.1.0 (prelude: standard)`, in bold cyan where the prompt has color.

- `--user` runs `~/.cmdr/prelude.cmdr` in place of the standard prelude, and the banner shows its path. A missing file is an error, not a fallback to the standard prelude.

- Line editing in the interactive REPL, with a vendored copy of linenoise: a light green prompt, highlighting, completion of command names, history in `~/.cmdr_history`, and Ctrl-C to drop the current pipeline. `-DCMDR_EDITOR=OFF` builds without it. linenoise compiles with C extensions: under strict C11, `strdup` and `fdopen` are undeclared, their `int` returns truncate pointers, and the REPL crashed on Enter.

  linenoise keeps a paste's newlines in one buffer, so pasted lines ran as one pipeline: `text a` and `text b` printed `a text b`. They now run one at a time. The vendored copy switches terminal modes with `TCSADRAIN`, not `TCSAFLUSH`, which discarded a second line typed or pasted before the first returned.

- `src/prelude.cmdr` defines `last`, the last item; `path-ext`, the text after the last dot; and `path-has-ext`, which keeps names with a dot after the first byte.

- `load PATH` runs a script in the current session, so its definitions remain.

- `def NAME BLOCK [DOC]`: `help` shows DOC, and the usage column shows the block's parameters, such as `exts DIR`.

### Fixed

- `cmdr_add_extension_tests` put `tests/` on a C test program's include path only when called from `tests/`. An example's `ext/NAME/tests/*.c` could not find `ext_check.h`.

- `map`'s help and the README say its COMMAND is looked up when `map` runs, not with the line. `each { COMMAND }` is checked first.

- Text-type errors name the command and the kind received: `sort: expected text, got a list`, not `expected text`. A block passed to a text-only command no longer suggests `map` or `join`, which apply to lists.

- `def` rejects a name starting with `{`. The name could not be called, since `{` there starts a block.

- `script-dir` and `import`'s search no longer call `dirname(3)`. POSIX lets it return static storage, which sessions on separate threads would share.

- `cmdr_realloc` with size 0 allocates, as with count 0. It divided by the size and raised SIGFPE.

- `load` documents that a relative PATH resolves from the current directory. Kept over script-relative resolution, which `import` uses, because that would break scripts that load from the cwd; `script-dir | path-join` gives the script-relative form.

- Ctrl-C while a line runs exits cmdr by SIGINT after stopping the line's program. cmdr died at once from the default SIGINT, so a program that ignored SIGINT kept running, unreaped, after cmdr exited. The program now gets SIGTERM, then SIGKILL a second later, as with `run-with`'s timeout. Exiting by the signal, not with a status, lets a calling shell loop stop. Hosts get the same through `cmdr_interrupt` and `cmdr_interrupted`.

- `shlex.join` uses the `lines` primitive. It called `lines` by name in the caller's scope, so a session `def lines` changed how it split text.

- A value nested too deeply to copy no longer leaks a block's code. The depth check ran after the copy had retained the block's compiled code, and the failure path did not release it.

- A line ending in `\\|` continues. An escaped backslash before the `|` was taken as escaping the `|`. A `\` inside single quotes before a final `|` still stops continuation.

- `src/session.c` declares `realpath`. `_POSIX_C_SOURCE` alone omits it, an XSI function, so builds without `_FORTIFY_SOURCE` called it as an implicit `int` function and truncated its pointer result. The build now fails on any implicit declaration.

- Programs no longer inherit the descriptors of the scripts that run them. `load`, `import`, and `cmdr FILE` opened scripts without close-on-exec, so `run` passed each open script to the child.

- `cmdr_push` doubles a list's allocation when full, tracked in the new field `cmdr_value.capacity`. It reallocated per item, so building N items copied O(N^2) items wherever `realloc` moved the block. 10^7 items timed out under AddressSanitizer. A capacity of 0 means exactly `count`, so a host that allocates a list whole leaves it 0.

- `tests/test_api.c` builds on macOS, which has no `sem_timedwait`. The thread test now meets on a mutex and condition variable.

- A block passed into a def ran its commands in the def's scope, not where it was written. Its names were checked against the caller's commands but resolved against the def's, so `apply { later }` failed with `unknown command: later` when `later` was defined after `apply`, and a session `def upper` was silently bypassed in favor of the primitive. Each line now compiles against the definitions current at its start, and its blocks keep that scope.

- `sort` on an empty list passed NULL to `qsort`, which is undefined behaviour.

- A failed history save no longer truncates `~/.cmdr_history`. linenoise truncated the file before rewriting it and ignored write errors, so a crash or full disk mid-save emptied the history. cmdr now saves to `~/.cmdr_history.tmp` and renames it over the old file only after a complete write; the vendored linenoise patch is noted in `thirdparty/linenoise/VENDOR`.

- `files` on an empty directory passed NULL to `qsort`, which is undefined behaviour.
