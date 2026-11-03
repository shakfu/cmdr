# TODO

## Critical

## High

- [ ] **1.0.** The contract is [docs/stability.md](docs/stability.md).

  - A deprecation warning, printed once per session to stderr.

## Medium

- [ ] **Sublime and VS Code grammars are not checked against the corpus.** `test_syntax_corpus` runs cmdr, tree-sitter, and Vim over `tests/syntax/corpus.txt`; it has no engine for the other two grammars' regex flavours.

- [ ] **`midi` and `snd` need the standard prelude.** `examples/midi/ext/midi/midi.cmdr` and `examples/snd/ext/snd/snd.cmdr` call `words`, `first`, and other prelude commands, so neither imports under a `--user` prelude that lacks them. `test_modules_without_prelude` checks only `ext/`.

- [ ] **Tree-sitter follow-ups.**

  - Zed: set `rev` in `editors/zed/extension.toml` once a commit contains `editors/tree-sitter-cmdr`, and install the dev extension. Zed is untested; Helix builds the grammar and finds the queries.

  - Neovim could use the grammar through nvim-treesitter in place of `editors/vim/syntax/cmdr.vim`. `queries/highlights.scm` already uses its capture names.

## Low
