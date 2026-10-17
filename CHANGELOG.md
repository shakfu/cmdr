# Changelog

## Unreleased

### Changed

- **Blocks replace string bodies and positional `$N`.** `{ ... }` is a
  pipeline passed as a value. Its command names and references are checked
  with the enclosing line, before any stage runs. Blocks declare parameters,
  which resolve lexically through nested blocks. Lexical scope over per-call
  `$N`, because per-call scope passed each outer value down at every nesting
  level and could not return a closure. See
  [docs/dev/blocks.md](docs/dev/blocks.md).

  ```text
  def wrap { (x d) text $d $x $d }
  def without { (xs v) keep { not { equals $v } } }
  list a.txt b.txt | each { try { read } "" }
  ```

  Block arity is exact: `{ ... }` takes one input and `{ () ... }` takes none.
  Before, `each { (n) try { add 1 } $n }` named the item as the fallback,
  which stopped the item being piped into `try`. `try` then ran its body
  without input and returned the fallback with no error. It now fails with
  `try: block takes 1 argument, got 0`.

  Breaking: `def NAME 'PIPELINE'`, `$1..$N`, and string bodies for `each`,
  `keep`, `not`, and `try` are now errors. `cmdr_check` is removed, and
  `cmdr_eval` no longer takes arguments.

### Fixed

- `files` on an empty directory passed NULL to `qsort`, which is undefined
  behaviour.
