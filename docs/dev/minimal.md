# Minimal cmdr

The `minimal` branch reduces the C implementation at `532dc6c`. C source went from 2384 to 1284 lines, and the binary from 234 KB to 175 KB. [compose-ng.md](compose-ng.md) predates this change and still describes `as` and captured failures.

## Removed

| Feature | Replacement | Reason |
|-|-|-|
| linenoise line editor | plain `getline` | 217 lines, a sibling checkout, and a PTY test, for editing and history |
| `as NAME`, `$name` | `$N` parameters and `$N` value stages | Needed frames that pass bindings into nested strings |
| Captured failures, `recover` | `try [INPUT] PIPELINE FALLBACK` | A fourth value kind with pass-through rules, a recursive scan, and per-command checks |
| `when` | `try 'COND \| THEN' ELSE` | Its arity rules differed by branch; only error vs mismatch in THEN differs from `try` |
| `tee`, `slice`, `print` | `write`; `drop \| take`; `write VALUE /dev/stderr` | Duplicates |
| Embedding API, `libcmdr.a`, `user` pointer | Edit `src/main.c` | No external users; five public entry points and two callback types |
| Recovery from out-of-memory | Process exits | An error branch after most allocations |

## Behavior changes

- `try` checks its body first. Syntax, name, and arity errors are reported, not replaced by FALLBACK, as `keep` and `not` already did.

- `try` needs FALLBACK, so `try BODY` with one argument is a usage error. With a piped value, `try` takes three arguments; without, two.

- Freeing, cloning, and printing values recurse. `cmdr_clone` rejects values nested more than 1024 deep. Every stage clones its input or builds flat lists, so no value grows deeper than that limit.

- Composed-command depth is a counter in `cmdr_call`, not a field copied on each call.

Making list commands always return lists was tried and reverted. It saved about 10 lines, but `read FILE | sort | upper` then needed `map upper`.
