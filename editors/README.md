# Editor support

Highlighting for `.cmdr` files. Vim, Sublime Text, and VS Code use regular-expression grammars, and each marks:

- comments, on lines that start with `#`
- the command in each stage
- `def` and the name it defines
- block parameters, `$name` references, quoted strings, escapes, numbers, and `|`

The grammars match the lexer in `src/cmdr.c`. They color a command by its position, without checking that it exists. The REPL marks unknown commands in red.

| Editor | Files | Install |
|-|-|-|
| Vim, Neovim | `vim/` | `set rtp+=/path/to/cmdr/editors/vim` in `.vimrc`, or copy the subdirectories into `~/.vim` |
| Sublime Text | `sublime/` | Copy the directory to `Packages/cmdr` (Preferences > Browse Packages) |
| VS Code | `vscode/` | `ln -s /path/to/cmdr/editors/vscode ~/.vscode/extensions/cmdr` |
| Helix | `helix/languages.toml` | Append to `~/.config/helix/languages.toml`. Detection and comments only |
| Zed | none | Needs a tree-sitter grammar |

Helix and Zed highlight only through tree-sitter grammars, which cmdr lacks; see [TODO.md](../TODO.md).

`tests/test_cmdr.py` checks the Vim grammar's groups in headless Vim, and checks that the other files parse.
