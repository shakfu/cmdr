# TODO

- [ ] **Tree-sitter grammar for cmdr.** Helix and Zed highlight only through tree-sitter, so `editors/` has no highlighting for either.
  - Write `grammar.js` from the lexer in `src/cmdr.c`: comments, stages, words, quotes, escapes, `$name`, and blocks with `(a b)` parameters.
  - Add `queries/highlights.scm` with the token classes in [editors/README.md](editors/README.md).
  - Helix: add `[[grammar]]` and `highlights.scm` under `runtime/queries/cmdr/` to `editors/helix/`.
  - Zed: add an extension with `extension.toml`, `languages/cmdr/config.toml`, and `highlights.scm`.
  - Neovim could use the grammar through nvim-treesitter in place of `editors/vim/syntax/cmdr.vim`.
  - Test with `tree-sitter test` corpus files covering the lexer cases in `tests/test_cmdr.py`.
