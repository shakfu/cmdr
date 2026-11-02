# TODO

## Critical

## High

## Medium

- [ ] **Value model.** Decide on [docs/dev/rich-types.md](docs/dev/rich-types.md) when a script needs sorting by number or stored booleans. JSON no longer depends on it: `ext/json` keeps JSON as text.

- [ ] **Tree-sitter follow-ups.**

  - Zed: set `rev` in `editors/zed/extension.toml` once a commit contains `editors/tree-sitter-cmdr`, and install the dev extension. Zed is untested; Helix builds the grammar and finds the queries.

  - Neovim could use the grammar through nvim-treesitter in place of `editors/vim/syntax/cmdr.vim`. `queries/highlights.scm` already uses its capture names.

- [ ] Extension modules run at their first `import`, after the prelude, which `--user` may replace. So `ext/*/*.cmdr` use only primitives and other modules, not prelude definitions such as `first`; nothing checks this. A check could import every extension module under an empty `--user` prelude in `make test`.

## Low
