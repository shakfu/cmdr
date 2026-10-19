# Changelog

## Unreleased

### Changed

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

- `docs/dev/scenarios-of-use.md` marks regex capture and environment access done, through `ext/re` and `ext/env`, and shows D1 log triage with `re-groups`.

### Added

- **`import NAME [as ALIAS]`** runs a module once per session and binds its definitions as `NAME.DEF`, as Python's `import` does. It searches the importing file's directory, `CMDR_PATH`, `~/.cmdr/lib`, and `share/cmdr/lib`; a package `NAME/__init__.cmdr` wins over `NAME.cmdr`. The module's other names stay inside it. Qualified names over a required `NAME-` prefix, because moving every module and importer to qualified names later would cost more with each module written. See [docs/dev/import.md](docs/dev/import.md).

- **`run-with OPTIONS PROGRAM [ARG ...]`** runs a program with `dir`, `stdin`, and `timeout` options. It returns `STATUS STDOUT STDERR`, where `run` fails on a nonzero status. List arguments are flattened, so an optional flag is an empty or two-item list. One command with options over a `run` variant per option, since the options combine. See [docs/dev/run-with-script-push.md](docs/dev/run-with-script-push.md).

- **`script-file` and `script-dir`** return the absolute path and directory of the script file in which they are written, like `realpath(__file__)` in Python, so a script can find its own files. A `def` in `lib/x.cmdr` whose body uses `script-dir` returns `lib`, even when a script elsewhere calls it. The alternative, the running script, would give that `def` its caller's directory.

- **Script arguments.** `cmdr FILE [ARG ...]` and `load PATH [ARG ...]` pass arguments, which `script-args` returns as a list. It resolves as `script-file` does, so a loaded library sees its own arguments, not the top script's. A process-wide list, like Python's `sys.argv`, would let a library depend on its caller's command line. `cmdr_session_load_args` passes arguments from a host. See [docs/dev/run-with-script-push.md](docs/dev/run-with-script-push.md#script-args).

- Extension `shlex`: `shlex-split`, `shlex-quote`, and `shlex-join`, as Python's `shlex` with `posix=True`. `shlex-split "git commit -m 'two words'"` gives four words, which `split` cannot, and nothing is expanded or run. See [ext/shlex](ext/shlex/README.md).

- `path-join BASE PATH` joins two paths with one `/`, as Python's `os.path.join` does; an absolute PATH replaces BASE. `path-split PATH` returns `path-dirname` and `path-basename` of PATH, like `os.path.split` but with POSIX rules: `path-split c` gives `.` and `c`. Both are prelude `def`s, so `script-dir | path-join lib.cmdr | load` needs no `concat`.

- `push LIST [ITEM ...]` appends items to a list. `list $xs x` nests `$xs` as one item, so `fold` could not collect results.

- `examples/agent-workflows`: chains of coding agents (claude, codex, cursor, pi, opencode) over a project, bug, or feature target. It is the package `aw`, used with `import aw`; `aw.cmdr` is its command-line entry point. `demo.cmdr` fixes a reported bug in a sample project with one agent. CTest runs the demo, `aw.cmdr`, and a test of every entry point with a dry agent. The `llama` agent sends the tracked files to llama-server and applies the edit blocks in its reply, retrying once with the reasons any failed; it runs no commands, so it needs no sandbox. The `claude` adapter fails before it starts if `bwrap` cannot create a network namespace, as under Ubuntu's `apparmor_restrict_unprivileged_userns`. Claude Code's `failIfUnavailable` did not catch this, and every Bash call failed while the step exited 0.

- `examples/sox-dsl`: sox in cmdr. The `snd` extension runs sox's arguments in-process through libsox: all 62 effects, the six `--combine` methods, format options, the null file, and the default audio device for play and record. It adds `varispeed`, which sox lacks: speed changes linearly over the output, on libsoxr's variable-rate mode. The `snd` module builds chains as lists of those arguments and splits files at times, at silences, and by channel. libsox over libsoxr with effects written here, because it covers sox's effects with sox's own arguments and DSP. Configure builds `cmdr-snd` only when pkg-config finds libsox and libsoxr, so `cmdr` depends on neither. See [examples/sox-dsl](examples/sox-dsl/README.md).

- **Extensions in `ext/NAME/`.** Each holds C commands, `.cmdr` definitions, a `README.md`, and `tests/`, and the build compiles them into `cmdr`. `make test` runs each `tests/*.cmdr` with `cmdr` and builds each `tests/*.c` against the extension. `help` shows C commands with `ext/NAME:` before the docstring. An extension command that reuses an existing name stops startup. A shadowed name would silently change prelude definitions, which bind names when defined. Extension scripts run before the prelude, because `--user` replaces it. See [README.md](README.md#extensions) and [ext/env](ext/env/README.md).

- Extensions `re`, `num`, `fs`, `time`, and `str`. Each covers a gap a `def` cannot fill: `matches` returns no captures, a `def` cannot loop N times, and no primitive calls `stat`. Every command starts with its extension's name, so a later primitive cannot collide with it. `time-local` and `time-utc` take `[EPOCH] FORMAT` over `FORMAT [EPOCH]`, so a piped time is formatted: `fs-mtime f | time-local %F`. See [README.md](README.md#extensions).

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

- `cmdr_push` doubles a list's allocation when full, tracked in the new field `cmdr_value.capacity`. It reallocated per item, so building N items copied O(N^2) items wherever `realloc` moved the block. 10^7 items timed out under AddressSanitizer. A capacity of 0 means exactly `count`, so a host that allocates a list whole leaves it 0.

- `tests/test_api.c` builds on macOS, which has no `sem_timedwait`. The thread test now meets on a mutex and condition variable.

- A block passed into a def ran its commands in the def's scope, not where it was written. Its names were checked against the caller's commands but resolved against the def's, so `apply { later }` failed with `unknown command: later` when `later` was defined after `apply`, and a session `def upper` was silently bypassed in favor of the primitive. Each line now compiles against the definitions current at its start, and its blocks keep that scope.

- `sort` on an empty list passed NULL to `qsort`, which is undefined behaviour.

- A failed history save no longer truncates `~/.cmdr_history`. linenoise truncated the file before rewriting it and ignored write errors, so a crash or full disk mid-save emptied the history. cmdr now saves to `~/.cmdr_history.tmp` and renames it over the old file only after a complete write; the vendored linenoise patch is noted in `thirdparty/linenoise/VENDOR`.

- `files` on an empty directory passed NULL to `qsort`, which is undefined behaviour.
