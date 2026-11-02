# TODO

## Critical

## High

- [ ] **1.0.** The contract is [docs/dev/stability.md](docs/dev/stability.md).

  - Act on the README.md and `docs/grammar.ebnf` audit, [docs/dev/audit-1.0.md](docs/dev/audit-1.0.md): 11 code fixes, 10 decisions, 28 documentation fixes, and 12 underspecified points.

  - A deprecation warning, printed once per session to stderr.

  - Version macros in `include/cmdr.h`, and the soname as MAJOR only from 1.0 (`CMakeLists.txt:58`).

  - CI on Linux and macOS, with an ASan and UBSan job. Nothing runs `make test` but a developer.

  - Fuzz targets for the parser and the `ext/json` scanner, which both read untrusted input.

  - One corpus of `.cmdr` files that every syntax implementation accepts: `src/cmdr.c`, `src/editor.c`, the Vim, Sublime, and VS Code grammars, and tree-sitter. Nothing checks that they agree.

  - Extension modules run at their first `import`, after the prelude, which `--user` may replace. So `ext/*/*.cmdr` use only primitives and other modules, not prelude definitions such as `first`; nothing checks this. A check could import every extension module under an empty `--user` prelude in `make test`.

  - Close the open questions in `docs/dev/ext-json.md`; its status line answers them.

  - Link stability.md from README.md, and drop its status line.

## Medium

- [ ] **Tree-sitter follow-ups.**

  - Zed: set `rev` in `editors/zed/extension.toml` once a commit contains `editors/tree-sitter-cmdr`, and install the dev extension. Zed is untested; Helix builds the grammar and finds the queries.

  - Neovim could use the grammar through nvim-treesitter in place of `editors/vim/syntax/cmdr.vim`. `queries/highlights.scm` already uses its capture names.

## Low
