# Janet kernel

cmdr's language is [src/cmdr.janet](../../src/cmdr.janet), hosted by
[src/main.c](../../src/main.c). It replaced the C interpreter (`cmdr.c`,
`value.c`, `prelude.c`, `essentials.c`, `commands.c`) and its embedding API
(`include/cmdr.h`). A prototype first ran the C suite and the README examples
against both implementations; they matched.

## Decisions

- **Janet over a hand-written core.** Janet replaced the C plumbing: value
  ownership and cloning, the tokenizer (now one PEG), process spawning, and
  error propagation. cmdr's own rules still had to be written: input piped
  into the first argument, failure pass-through, mismatch status, and arity.
- **`as NAME` removed.** Named bindings were the only reason pipelines needed
  closures over enclosing scopes. In C they needed a frame chain. In Janet,
  strings compiled at run time (`map try BODY`) could not see them, so the
  two implementations diverged. Without `as`, every pipeline string compiles
  the same way, when it runs. Intermediate values are passed to a `def` as
  `$N` parameters.
- **Closures, not generated code.** Each stage compiles to a Janet closure;
  `eval` and AST generation are not used.
- **C API dropped.** Commands are added with `register`. C functions go
  through `janet_cfuns`. `tests/test_api.janet` replaces `test_api.c`.
- **Line editor kept.** `editor.c` stays in C. The host passes it the active
  command names on each read, through `use-editor`.

## Costs

Measured on Linux x86-64 against the last C build (`532dc6c`):

| | C | Janet |
|-|-|-|
| Language and host, excluding blanks and comments | 1792 lines | 549 Janet + 72 C |
| Binary (static `libjanet.a`) | 234 KB | 2.4 MB |
| `list` of 20k items through `map`, `each`, `keep` | 0.03 s, 7 MB | 0.23 s, 17 MB |

Janet's `try` and `defer` each create a fiber. Using `defer` on every command
call cost about 2x; state is now restored only where errors are caught.

## Behavior changes

- `as` is an ordinary name; `$name` is an invalid reference.
- File errors infer their message from `os/stat`, since Janet does not
  expose `errno`.
- Counts and references longer than 15 digits are rejected (doubles).

## Vendoring

Janet and linenoise are in `thirdparty/`, with provenance in each `VERSION`.
A clean clone builds without network access or sibling checkouts, and the
Janet version is pinned. The cost is 3.6 MB of source and about 10 s to
compile `janet.c` on a clean build. `CMDR_JANET_DIR` links an external Janet
instead.

- Janet: the release amalgamation, byte-identical to `make` output at tag
  v1.42.1. The Janet repository has no amalgamation, so a submodule would need
  Janet's bootstrap build.
- linenoise: the core library only. CMake builds it directly, without its own
  `CMakeLists.txt`.

linenoise declares no POSIX feature macros. Built as strict C, `strdup` and
`fdopen` were undeclared, and their implicit `int` returns truncated pointers.
The REPL segfaulted on Enter, in the C build too. It now builds as gnu99, as
it does standalone.
