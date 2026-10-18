# Changelog

## Unreleased

### Changed

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

- `last` fails with a mismatch on an empty list or text; it returned empty text, which could not be told apart from an empty last item. `first` does the same, and so does `ext` on empty text.

- Mismatch messages name the values involved: `equals: foo differs from bar`, `not: [1] satisfies { nonempty }`. They omitted the tested value, so a composed predicate's failure did not say which input failed. A mismatch from a def called on a line also shows the call's arguments, `ends-with ar bar: ...`; errors keep the plain call chain, which serves debugging. Values print in one line, up to 32 bytes and 5 items.

- Each test method is a CTest test, read from `tests/test_cmdr.py` at configure time, and `make test` runs them in parallel.

### Added

- Primitives `with`, `fold`, `or`, `check`, `matches`, `sort-n`, `le`, and `ge`. `with` passes a computed value to any argument position, which a pipe cannot. `or` is `try` restricted to mismatches, so a conditional does not hide errors as `try` does. `check` replaces a mismatch's message; errors pass through. `matches` uses POSIX extended regular expressions. `ge` and `le` are primitives rather than `not { lt }`, so a failure reads `ge: 4 is not at least 5`.

  ```text
  text foobar | with { (s) length foo | with { (n) drop $s $n } }
  list 3 10 2 | fold 0 { (a x) or $a { gt $x } $x }
  ```

- The prelude defines list commands (`first`, `nth`, `init`, `rest`, `concat`, `unlines`, `unwords`, `distinct`, `reject`, `without`, `compact`, `prefix`, `suffix`, `empty`, `any`, `all`), numbers (`sum`, `product`, `max`, `min`, `mean`, `between`), text (`words`, `starts-with`, `ends-with`, `strip-prefix`, `strip-suffix`), and paths and programs (`stem`, `cat`, `grep`, `sh`, `sh-in`). Each is composed from primitives.

- Version 0.1.0, set in `CMakeLists.txt`. `cmdr --version` prints it, and the REPL starts with `cmdr 0.1.0 (prelude: standard)`, in bold cyan where the prompt has color.

- `--user` runs `~/.cmdr/prelude.cmdr` in place of the standard prelude, and the banner shows its path. A missing file is an error, not a fallback to the standard prelude.

- Line editing in the interactive REPL, with a vendored copy of linenoise: a light green prompt, highlighting, completion of command names, history in `~/.cmdr_history`, and Ctrl-C to drop the current pipeline. `-DCMDR_EDITOR=OFF` builds without it. linenoise compiles with C extensions: under strict C11, `strdup` and `fdopen` are undeclared, their `int` returns truncate pointers, and the REPL crashed on Enter.

  linenoise keeps a paste's newlines in one buffer, so pasted lines ran as one pipeline: `text a` and `text b` printed `a text b`. They now run one at a time. The vendored copy switches terminal modes with `TCSADRAIN`, not `TCSAFLUSH`, which discarded a second line typed or pasted before the first returned.

- `src/prelude.cmdr` defines `last`, the last item; `ext`, the text after the last dot; and `has-ext`, which keeps names with a dot after the first byte.

- `load PATH` runs a script in the current session, so its definitions remain.

- `def NAME BLOCK [DOC]`: `help` shows DOC, and the usage column shows the block's parameters, such as `exts DIR`.

### Fixed

- A block passed into a def ran its commands in the def's scope, not where it was written. Its names were checked against the caller's commands but resolved against the def's, so `apply { later }` failed with `unknown command: later` when `later` was defined after `apply`, and a session `def upper` was silently bypassed in favor of the primitive. Each line now compiles against the definitions current at its start, and its blocks keep that scope.

- `sort` on an empty list passed NULL to `qsort`, which is undefined behaviour.

- A failed history save no longer truncates `~/.cmdr_history`. linenoise truncated the file before rewriting it and ignored write errors, so a crash or full disk mid-save emptied the history. cmdr now saves to `~/.cmdr_history.tmp` and renames it over the old file only after a complete write; the vendored linenoise patch is noted in `thirdparty/linenoise/VENDOR`.

- `files` on an empty directory passed NULL to `qsort`, which is undefined behaviour.
