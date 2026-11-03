# Stability decisions

Decision record, 2026-11-02. The contract these decisions produce is [docs/stability.md](../stability.md). The remaining 1.0 work is in [TODO.md](../../TODO.md).

## Implementation notes

- Command status codes: `CMDR_MISMATCH`, `include/cmdr.h:15`.

- Limits: `MAX_CALL_DEPTH` and `MAX_VALUE_DEPTH`, `src/cmdr.c:13`.

- The version lives in `include/cmdr.h`. The soname and `find_package` compatibility are MAJOR.MINOR while MAJOR is 0, and MAJOR from 1.0 (`CMakeLists.txt`).

- cmdr has no deprecation warning yet.

## Decisions before 1.0

1. **Value model.** Decided 2026-11-02: text-first, with dicts as a separate kind (`ext/dict`). [rich-types.md](rich-types.md)'s numbers, bools, and null are rejected: they change unquoted `3`, `true`, and `null` and the result of `sort`, so they would be a 2.0.

2. **C ABI.** Decided 2026-11-02: the C API is stable, and `cmdr_value` is opaque with a fixed size of 6 pointers. Pointer handles were the alternative: they free the size too, but change every callback's signature and allocate per result.

3. **Stable modules.** Decided 2026-11-02: every module in `ext/` is versioned, and `examples/` is unstable. `dict` and the `json` dict commands are days old; listing them still lets them change, with a Breaking entry and a deprecation period. On 2026-11-03, `base64`, `csv`, `glob`, `hashlib`, and `io` joined the 15, along with `fs.each-line` and `fs.fold-lines`. They are versioned on the same terms. `glob.glob` reserves `**` as an error, and the `fs` line commands read stdin for `-`, so neither meaning changes in 1.x.

4. **Call depth.** Decided 2026-11-02: the depth limits are incidental, not a termination guarantee, so recursion stays possible in 1.x. See [similarities.md](similarities.md).

5. **`--fmt` output.** Decided 2026-11-02: stable within a major release, so `cmdr --fmt --check` can run in CI across 1.x. The cost: layout improvements wait for 2.0.

6. **Numbers.** Decided 2026-11-03: arithmetic stays on doubles, as README.md documents, so integers are exact only below 2^53 and print in exponent form from 1e15. An integer path in `add`, `sub`, and `mul` was the alternative; it changes those results for existing inputs, so it would be a 2.0. Exact integers may come in 1.x as a module.

## After 1.0

These add to the language without breaking it: [async.md](async.md) options B to E, a `cmdr-exe` primitive, a module of exact integer arithmetic, the agent-runs open questions, and the Tree-sitter follow-ups.
