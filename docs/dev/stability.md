# Stability

Decision record, 2026-11-02. Status: all five decisions made. The remaining 1.0 work is in [TODO.md](../../TODO.md).

## Tiers

| Tier | Covers | Promise in 1.x |
|-|-|-|
| Stable | Syntax, values, primitives, the standard prelude, command and process status, `import`, the command line, the C API and ABI | No breaking change |
| Versioned | Every module in `ext/` | No breaking change without a CHANGELOG entry marked Breaking and a deprecation period |
| Unstable | `examples/` and the editor grammars | May change in any minor release |

## Stable at 1.0

- **Syntax.** [grammar.ebnf](../grammar.ebnf) defines it. A script that parses under 1.0 parses the same under every 1.x.

- **Values.** A value is text, a list, a block, or a dict. Literals, `read`, and `run` produce text, and no existing command changes the kind it returns. A new kind may be added in 1.x only if no existing command returns it for existing inputs.

- **Primitives and the standard prelude.** Each documented command keeps its name, arguments, defaults, and result. A command that mismatches keeps mismatching, and one that errors keeps erroring, for the same inputs.

- **Command status.** 0 is a result, 1 an error, 2 a mismatch (`CMDR_MISMATCH`, `include/cmdr.h:9`).

- **Process exit.** 0 if every line succeeded, 1 if any failed, 2 for a usage error. cmdr exits by SIGINT or SIGTERM when it receives one.

- **`import`.** The search order in [README.md](../../README.md#modules), and `import`, `from`, and `as` semantics.

- **The command line.** The flags in [README.md](../../README.md#command-line).

- **`--fmt` output.** The layout changes only in a major release, so a file that passes `cmdr --fmt --check` under 1.0 passes under every 1.x. The exception is a formatter bug, fixed in any release:

  - output that runs differently from its input, or drops a comment;
  - refusing a script that runs;
  - output that changes when formatted again;
  - output that breaks the documented layout rules, such as the 100-column limit.

  After such a fix, a file may fail `--check` once. The CHANGELOG entry names the change. A layout that is merely worse is not a bug.

Not covered:

- Error and mismatch message text. Scripts should test status, not stderr.

- `help` text, the REPL banner, prompt, and key bindings.

- Performance, and the limits: 128 nested calls (`MAX_CALL_DEPTH`), 127 levels of blocks and sub-pipelines, and values nested 1024 deep (`MAX_VALUE_DEPTH`), in `src/cmdr.c:13`. A 1.x release may raise them, not lower them. They are not a termination guarantee: a minor release may add recursion.

## Versioning

Semantic versioning from 1.0:

- A patch release fixes bugs. Behaviour that contradicts the documentation is a bug, and the documentation wins.

- A minor release adds commands, modules, flags, or syntax that was an error before.

- A major release may break the stable tier.

**Deprecation.** A deprecated name keeps working for at least one minor release. Its first use in a session prints a warning to stderr. It is removed only in a major release. cmdr has no warning mechanism yet.

## C API

The soname is MAJOR.MINOR (`CMakeLists.txt:58`), so each 0.x minor release may break the ABI. At 1.0 it becomes MAJOR: `libcmdr.so.1`.

`cmdr_value`'s fields are private: hosts and extensions use accessors and constructors. Its size, 6 pointers, is part of the ABI, so values stay in arrays and on the stack; the layout may change within that size. `cmdr_kind` may gain members in a minor release, so a `switch` over it needs a default.

`include/cmdr.h` has no version macro. Extensions cannot check the API version at compile time.

## Decisions before 1.0

1. **Value model.** Decided 2026-11-02: text-first, with dicts as a separate kind (`ext/dict`). [rich-types.md](rich-types.md)'s numbers, bools, and null are rejected: they change unquoted `3`, `true`, and `null` and the result of `sort`, so they would be a 2.0.

2. **C ABI.** Decided 2026-11-02: the C API is stable, and `cmdr_value` is opaque with a fixed size of 6 pointers. Pointer handles were the alternative: they free the size too, but change every callback's signature and allocate per result.

3. **Stable modules.** Decided 2026-11-02: all 15 modules in `ext/` are versioned, and `examples/` is unstable. `dict` and the `json` dict commands are days old; listing them still lets them change, with a Breaking entry and a deprecation period.

4. **Call depth.** Decided 2026-11-02: the depth limits are incidental, not a termination guarantee, so recursion stays possible in 1.x. See [similarities.md](similarities.md).

5. **`--fmt` output.** Decided 2026-11-02: stable within a major release, so `cmdr --fmt --check` can run in CI across 1.x. The cost: layout improvements wait for 2.0.

## After 1.0

These add to the language without breaking it: [async.md](async.md) options B to E, a `cmdr-exe` primitive, the agent-runs open questions, and the Tree-sitter follow-ups.
