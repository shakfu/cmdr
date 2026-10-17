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

- Each test method is a CTest test, read from `tests/test_cmdr.py` at configure time, and `make test` runs them in parallel.

### Added

- Version 0.1.0, set in `CMakeLists.txt`. `cmdr --version` prints it, and the REPL starts with `cmdr 0.1.0 (prelude: standard)`, in bold cyan where the prompt has color.

- `--user` runs `~/.cmdr/prelude.cmdr` in place of the standard prelude, and the banner shows its path. A missing file is an error, not a fallback to the standard prelude.

- Line editing in the interactive REPL, with a vendored copy of linenoise: a light green prompt, highlighting, completion of command names, history in `~/.cmdr_history`, and Ctrl-C to drop the current pipeline. `-DCMDR_EDITOR=OFF` builds without it. linenoise compiles with C extensions: under strict C11, `strdup` and `fdopen` are undeclared, their `int` returns truncate pointers, and the REPL crashed on Enter.

  linenoise keeps a paste's newlines in one buffer, so pasted lines ran as one pipeline: `text a` and `text b` printed `a text b`. They now run one at a time. The vendored copy switches terminal modes with `TCSADRAIN`, not `TCSAFLUSH`, which discarded a second line typed or pasted before the first returned.

- `src/prelude.cmdr` defines `last`, the last item; `ext`, the text after the last dot; and `has-ext`, which keeps names with a dot after the first byte.

- `load PATH` runs a script in the current session, so its definitions remain.

- `def NAME BLOCK [DOC]`: `help` shows DOC, and the usage column shows the block's parameters, such as `exts DIR`.

### Fixed

- `files` on an empty directory passed NULL to `qsort`, which is undefined behaviour.
