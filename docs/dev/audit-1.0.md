# 1.0 audit: README.md and grammar.ebnf

Status: resolved 2026-11-02; see CHANGELOG.md Unreleased.

Audit, 2026-11-02, of the working tree after 0.5.0. Under [stability.md](stability.md) the documentation defines behaviour, so each finding is a code fix, a documentation fix, or a decision. IDs: G for `docs/grammar.ebnf`, L for README.md lines 1-498, U for the rest of README.md. Each was found by running `build/cmdr` or by reading the cited code; the entries marked (re-run) were reproduced a second time with `build-all/cmdr`.

## Code fixes

Bugs with one sane fix.

| ID | Where | Defect | Fix |
|-|-|-|-|
| G5 | `src/cmdr.c:496`, `src/format.c` | A NUL byte ends the line silently: `text a\0b c` prints `a`. `--fmt` drops the rest of the file, and `-w` writes that back. (re-run) | Reject NUL with an error at load and in `--fmt` |
| G1 | `src/format.c:396` | `--fmt` passes comment lines to `cmdr_check_syntax`, so a script that runs is refused: a comment holding `'` inside a multi-line block gives `unclosed quote`. (re-run) | Check each chunk as `cmdr_join` gives it |
| G3 | `src/cmdr.c:440`, `src/session.c:883` | `\` before a newline outside quotes puts a newline in the word; inside a block it stops joining (`unclosed '{'`). `--fmt` adds a newline per pass. (re-run) | Error: `unfinished escape`, as grammar.ebnf:61 says |
| G8 | `src/cmdr.c` | `(x x)` fails only by accident: `parameter $x is never used`. | Error: duplicate parameter |
| U2 | `src/format.c` | Not a defect: `#` after `\|` is text, so `list a b \| # c` is a pipeline whose second command is `#`. The finding read it as a comment: `--flat` leaves `list a b \| # c` plus a continuation on two lines. | None; grammar.ebnf documents that `--fmt` does not resolve names (G9) |
| L15 | `src/prelude.cmdr` | Parameter names disagree with docstrings: `reject XS T` "containing TEXT", `grep F P`, `path-join BASE P`, `starts-with S X`, `between N LO HI`. | Rename the parameters |
| L18 | `src/primitives.c` | `replace` reports `usage: replace TEXT NONEMPTY_SEARCH REPLACEMENT`; `help` says `replace TEXT OLD NEW`. | One usage string |
| U15 | `cmake/CmdrExtensions.cmake`, `src/session.c:593` | Two extensions defining one module name: the first registered wins, without a diagnostic. Inferred from code. | Configure error |
| U23 | `libcmdr.so` | Exports `cmdr_prelude_source` and `cmdr_prelude_effects_source`, which no header declares. They would freeze into the 1.0 ABI. | Hidden visibility |
| U14 | `examples/midi/extension.cmake` | Checks for a C++ compiler, not C++20; libremidi needs C++20. Inferred, not tested. | `cxx_std_20` check |
| U21 | `Makefile` | `make check` reconfigures with `CMAKE_BUILD_TYPE=RelWithDebInfo`, replacing the user's type. | Pass the type only when set |

## Decisions

Each could be fixed in the code or the documentation. Recommendation first.

| ID | Finding | Recommendation | Alternative |
|-|-|-|-|
| L1 | `or BLOCK FALLBACK` returns a block FALLBACK unevaluated; `try` calls it as a handler. (re-run) | Call it with the mismatch message, as `try` does | Document that FALLBACK is never called |
| L2 | `map`'s COMMAND is resolved at run time, so `text x \| write f \| map uppr` writes `f`, then fails. README.md:23 says a typo fails the line first. (re-run) | Check a literal COMMAND word when compiling | Exempt `map` in the intro |
| L4 | `as` inside a def's body binds for the whole session: `def mk { (x) text $x \| as inner }`, `mk 5`, then `inner` prints `5`. `def` in a block that uses enclosing parameters is an undocumented error. (re-run) | `as` and `def` inside a block are errors | Document session-wide binding |
| G2 | `#` on a `{` or `$(` line is not a comment to bracket matching: `{ # a } b` closes the block. Joining treats it as a comment. (re-run) | Skip comments in bracket matching | Document that such comments are code |
| G7 | Nesting limits count each bracket kind separately, raw `(` included; syntax allows 128 levels, evaluation 127. | One depth over blocks and sub-pipelines, ignoring plain `(`; one limit | Document the rule as it is |
| L6 | `nth` parses its index as a number (`nth a 1.0` and `01` work); `head`, `take`, and `retry` use `cmdr_natural` and reject `1.0`. (re-run) | `cmdr_natural` everywhere; `nth` keeps its mismatch for 0 and past the end | Accept integral numbers everywhere |
| U4 | Prelude definitions shadow a host's commands: a host `last` is ignored. README.md:572 says host commands shadow the primitives. | Host commands shadow the prelude too | Document the order |
| U8 | Without effects there is no `import`, so a pure host cannot import modules it registered in memory. | `import` without effects, finding only registered modules | Document |
| U9 | With `TERM=dumb` or `-DCMDR_EDITOR=OFF`, Ctrl-C at the prompt ends cmdr; README.md:511 says it drops the pipeline. | Drop the line in plain mode too | Scope the claim to the editor |
| L16 | `split "" ,` gives 0 items; Python's `''.split(',')` gives `['']`. | Keep 0, as `lines` does, and document | Python's `['']` |

## Documentation fixes

The code is right; the text is wrong or stale.

- G4 grammar.ebnf:117: `$( # x )` is rejected when compiled, not when run.
- G6 grammar.ebnf:55: inside a block or sub-pipeline, any unquoted `{` or `(` counts toward nesting, so `a{b` is literal only at top level.
- G9 grammar.ebnf:113: `cmdr_check_syntax` does not resolve names or check unused parameters. Mark which constraints `--fmt` enforces.
- G10 grammar.ebnf:3: replace "the code is correct" with the 1.0 rule. Name every function the rules live in. Restate `script` as joining, then parsing each joined unit as `body`.
- L5 README.md:483: the quoted `last` differs from `src/prelude.cmdr:8`.
- L7 README.md:377: any `from ... import X as Y` may shadow, not only `DEF as DEF`.
- L8 README.md:146: the example needs `import random`.
- L9 README.md:235, L10 README.md:315: examples show no output, or output without its trailing space.
- L11 README.md:425: `load`, `import`, and `from` are session commands, not primitives.
- L12 README.md:67: `sort-n` also returns text for text input.
- L14 README.md:347: `script-dir` returns an absolute path.
- U1 README.md:626, `include/cmdr.h:69`: `cmdr_pure` also omits `run-with`, `run-all`, and `spawn`. (re-run)
- U3 README.md:524: `--fmt` breaks only blocks and sub-pipelines over 100 columns; it joins shorter ones.
- U5 README.md:525: `--user` applies to the REPL, `-c`, and FILE only. (re-run)
- U6 README.md:646: an error in the second prelude file reports `prelude-effects.cmdr:LINE:`. (re-run)
- U7 README.md:546: the quoted `ext/fs/fs.cmdr` is stale.
- U10 README.md:523: `-c` stops at the first failure, without a line prefix, and has no `script-*`.
- U11 README.md:528: exit statuses are 0, 1, and 2 for usage, or death by SIGINT or SIGTERM.
- U12 README.md:524: options come before FILEs.
- U13 README.md:677: Python is optional with tests on; `make install` needs it.
- U16 README.md:566: extension tests also need `ext_commands`.
- U17 README.md:542: both checks fail at configure; an extension with only `.cmdr` files needs no `NAME.cmdr`.
- U18 README.md:568: `mk_ext.py` requires names matching `[a-z][a-z0-9]*(-[a-z0-9]+)*`.
- U19 `include/cmdr.h:198`: the session adds `load` only with effects.
- U20 README.md:507: a `$NAME` reference is plain magenta; only a use as a command is bold.
- U22 README.md:648: `make install` and `PREFIX` are undocumented.
- U23 README.md, Embedding: `cmdr_free` and `cmdr_clone` are not named, and the snippet leaks `value`.

## Underspecified

True, but a 1.0 user would trip on them. Each needs a sentence.

- L3 README.md:302: with `out` and `err` naming one file, STDOUT and STDERR are both the merged contents. Relative paths resolve from cmdr's directory, not `dir`.
- L13 README.md:56: printing adds a final newline; `list`, `list ""`, and `text ""` print alike; a nested list prints its items, then a blank line.
- L17 README.md:456: `write` of an item holding a newline does not round-trip through `lines`.
- L19 README.md:56: "no value" is detected when the stage runs.
- L20 README.md:75: `\` at a line's end is an error (with G3), not a continuation.
- L21 README.md:341: a script is checked one pipeline at a time, so earlier lines run before a later line's typo fails.
- L22, L23, L24, L25, L26, L27: piped stdin continues after errors; `files` includes links to regular files and prefixes `./`; an indented `#` line is a comment; `''` is an empty default; numbers switch to exponent form as Python's `repr` does; the module table summarizes.

## Not checked

The regex cost figures (README.md:283), `dir` without `addchdir`, `spawn` reaping, terminal Ctrl-C for group 1, and the C++20 failure of U14 were not run.
