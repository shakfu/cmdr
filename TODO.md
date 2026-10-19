# TODO

## Critical

## High

## Medium

- [ ] **Tree-sitter grammar for cmdr.** Helix and Zed highlight only through tree-sitter, so `editors/` has no highlighting for either.
  - Write `grammar.js` from the lexer in `src/cmdr.c`: comments, stages, words, quotes, escapes, `$name`, and blocks with `(a b)` parameters.

  - Add `queries/highlights.scm` with the token classes in [editors/README.md](editors/README.md).

  - Helix: add `[[grammar]]` and `highlights.scm` under `runtime/queries/cmdr/` to `editors/helix/`.

  - Zed: add an extension with `extension.toml`, `languages/cmdr/config.toml`, and `highlights.scm`.

  - Neovim could use the grammar through nvim-treesitter in place of `editors/vim/syntax/cmdr.vim`.

  - Test with `tree-sitter test` corpus files covering the lexer cases in `tests/test_cmdr.py`.

- [ ] Extension scripts run before the prelude. `--user` replaces the prelude, so env.cmdr broke under it while working in a normal run. Running scripts first means a prelude dependency fails on every run instead. The cost is that extension .cmdr files can use only primitives and extension commands, not compact, first and the like. The alternative is to always run the standard prelude before extension scripts, which would change what `--user` means.

## Low
