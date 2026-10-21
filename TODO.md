# TODO

## Critical

## High

- [ ] **Structured data.** Records are words in text, read with `words | nth` (15 places), and JSON goes through `jq` with `run`. JSON is what glue scripts most exchange: APIs and agents' output. See [docs/dev/missing-syntax.md](docs/dev/missing-syntax.md), "Library gaps".
  - Decide between a map value beside text and lists, which changes the value model, `cmdr_value`, and every list command's contract, and a `json` extension that converts JSON to and from lists of key and value pairs, with no new value type.
  - A third option keeps JSON as text, read and built by path: [docs/dev/ext-json.md](docs/dev/ext-json.md).
  - The pairs form keeps values printable and checkable; its cost is linear lookup and no way to tell an empty object from an empty array.
  - Whichever form, `aw`'s llama adapter and `midi`'s items are the first users to convert.

## Medium

- [ ] **Tree-sitter follow-ups.**
  - Zed: set `rev` in `editors/zed/extension.toml` once a commit contains `editors/tree-sitter-cmdr`, and install the dev extension. Zed is untested; Helix builds the grammar and finds the queries.
  - Neovim could use the grammar through nvim-treesitter in place of `editors/vim/syntax/cmdr.vim`. `queries/highlights.scm` already uses its capture names.

- [ ] Extension modules run at their first `import`, after the prelude, which `--user` may replace. So `ext/*/*.cmdr` use only primitives and other modules, not prelude definitions such as `first`; nothing checks this. A check could import every extension module under an empty `--user` prelude in `make test`.

## Low
