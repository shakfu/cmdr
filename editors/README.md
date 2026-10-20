# Editor support

Highlighting for `.cmdr` files. Vim, Sublime Text, and VS Code use regular-expression grammars. Helix and Zed use the tree-sitter grammar in `tree-sitter-cmdr/`. Each marks:

- comments, on lines that start with `#`
- the command in each stage
- `def` and the name it defines
- `$(...)` sub-pipelines: `$(` and the `)` that balances it, with the first word inside as a command
- block parameters, including `_`, `$name` and `$MODULE.NAME` references, quoted strings, escapes, numbers, and `|`

The grammars match the lexer in `src/cmdr.c`. They color a command by its position, without checking that it exists. The REPL marks unknown commands in red.

| Editor | Files | Install |
|-|-|-|
| Vim, Neovim | `vim/` | `set rtp+=/path/to/cmdr/editors/vim` in `.vimrc`, or copy the subdirectories into `~/.vim` |
| Sublime Text | `sublime/` | Copy the directory to `Packages/cmdr` (Preferences > Browse Packages) |
| VS Code | `vscode/` | `ln -s /path/to/cmdr/editors/vscode ~/.vscode/extensions/cmdr` |
| Helix | `helix/` | Append `languages.toml` to `~/.config/helix/languages.toml` and set the grammar's path. Copy `runtime/queries/cmdr/` to `~/.config/helix/runtime/queries/cmdr/`. Run `hx --grammar build` |
| Zed | `zed/` | Set `rev` in `extension.toml` to a commit that contains `tree-sitter-cmdr/`, then run "zed: install dev extension" on the directory |

## Tree-sitter

`tree-sitter-cmdr/` has the grammar, `grammar.js`; the generated parser, `src/`; and `queries/highlights.scm`, in nvim-treesitter capture names. Helix and Zed keep copies of the query, Helix's in its own scope names.

After changing `grammar.js`, run `tree-sitter generate` and `tree-sitter test` in `tree-sitter-cmdr/`. `test/corpus/` holds expected parse trees, and `test/highlight/` holds highlight assertions.

The grammar marks what `src/cmdr.c` rejects as an `ERROR` or `MISSING` node: a sub-pipeline as a command, arguments after a lone reference, a trailing `|` at the end of the file, unclosed quotes and braces. It does not check what the compiler checks, such as unknown commands, unused parameters, or `expected a space after '}'`.

## Checks

`tests/test_cmdr.py` checks the Vim grammar's groups in headless Vim, and checks that the other files parse; the Sublime check needs PyYAML. With the `tree-sitter` CLI installed, it also checks that `src/` matches `grammar.js`, runs `tree-sitter test`, parses every `.cmdr` file in the repository, and compiles the three highlight queries.
