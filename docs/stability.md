# Stability

What cmdr promises across 1.x releases.

## Tiers

| Tier | Covers | Promise in 1.x |
|-|-|-|
| Stable | Syntax, values, primitives, the standard prelude, command and process status, `import`, the command line, the C API and ABI | No breaking change |
| Versioned | Every module in `ext/` | No breaking change without a CHANGELOG entry marked Breaking and a deprecation period |
| Unstable | `examples/` and the editor grammars | May change in any minor release |

## Stable

- **Syntax.** [grammar.ebnf](grammar.ebnf) defines it. A script that parses under 1.0 parses the same under every 1.x.

- **Values.** A value is text, a list, a block, or a dict. Literals, `read`, and `run` produce text, and no existing command changes the kind it returns. A new kind may be added in 1.x only if no existing command returns it for existing inputs.

- **Primitives and the standard prelude.** Each documented command keeps its name, arguments, defaults, and result. A command that mismatches keeps mismatching, and one that errors keeps erroring, for the same inputs.

- **Command status.** 0 is a result, 1 an error, 2 a mismatch (`CMDR_MISMATCH`).

- **Process exit.** 0 if every line succeeded, 1 if any failed, 2 for a usage error. cmdr exits by SIGINT or SIGTERM when it receives one.

- **`import`.** The search order in [README.md](../README.md#modules), and `import`, `from`, and `as` semantics.

- **The command line.** The flags in [README.md](../README.md#command-line).

- **`--fmt` output.** The layout changes only in a major release, so a file that passes `cmdr --fmt --check` under 1.0 passes under every 1.x. The exception is a formatter bug, fixed in any release:

  - output that runs differently from its input, or drops a comment;
  - refusing a script that runs;
  - output that changes when formatted again;
  - output that breaks the documented layout rules, such as the 100-column limit.

  After such a fix, a file may fail `--check` once. The CHANGELOG entry names the change. A layout that is merely worse is not a bug.

## Not covered

- Error and mismatch message text. Scripts should test status, not stderr.

- `help` text, the REPL banner, prompt, and key bindings.

- Performance, and the limits: 128 nested calls, 127 levels of blocks and sub-pipelines, and values nested 1024 deep. A 1.x release may raise them, not lower them. They are not a termination guarantee: a minor release may add recursion.

## Versioning

cmdr follows semantic versioning from 1.0:

- A patch release fixes bugs. Behaviour that contradicts the documentation is a bug, and the documentation wins.

- A minor release adds commands, modules, flags, or syntax that was an error before.

- A major release may break the stable tier.

**Deprecation.** A deprecated name keeps working for at least one minor release. Its first use in a session prints a warning to stderr. It is removed only in a major release.

## C API

The shared library's soname is the major version: `libcmdr.so.1` across 1.x.

`cmdr_value`'s fields are private: hosts and extensions use accessors and constructors. Its size, 6 pointers, is part of the ABI, so values can live in arrays and on the stack; the layout may change within that size. `cmdr_kind` may gain members in a minor release, so a `switch` over it needs a default.
