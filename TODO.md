# TODO

## Critical

## High

- [ ] **Structured data.** Records are words in text, read with `words | nth` (15 places), and JSON goes through `jq` with `run`. JSON is what glue scripts most exchange: APIs and agents' output. See [docs/dev/missing-syntax.md](docs/dev/missing-syntax.md), "Library gaps".
  - Decide between a map value beside text and lists, which changes the value model, `cmdr_value`, and every list command's contract, and a `json` extension that converts JSON to and from lists of key and value pairs, with no new value type.
  - The pairs form keeps values printable and checkable; its cost is linear lookup and no way to tell an empty object from an empty array.
  - Whichever form, `aw`'s llama adapter and `midi`'s items are the first users to convert.

## Medium

- [ ] **Tree-sitter grammar for cmdr.** Helix and Zed highlight only through tree-sitter, so `editors/` has no highlighting for either.
  - Write `grammar.js` from the lexer in `src/cmdr.c`: comments, stages, words, quotes, escapes, `$name`, `$(...)` sub-pipelines, and blocks with `(a b)` and `_` parameters.

  - Add `queries/highlights.scm` with the token classes in [editors/README.md](editors/README.md).

  - Helix: add `[[grammar]]` and `highlights.scm` under `runtime/queries/cmdr/` to `editors/helix/`.

  - Zed: add an extension with `extension.toml`, `languages/cmdr/config.toml`, and `highlights.scm`.

  - Neovim could use the grammar through nvim-treesitter in place of `editors/vim/syntax/cmdr.vim`.

  - Test with `tree-sitter test` corpus files covering the lexer cases in `tests/test_cmdr.py`.

- [ ] Extension modules run at their first `import`, after the prelude, which `--user` may replace. So `ext/*/*.cmdr` use only primitives and other modules, not prelude definitions such as `first`; nothing checks this. A check could import every extension module under an empty `--user` prelude in `make test`.

## Low
