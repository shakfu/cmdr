# Composability review

Review of composition limits as of `5aa354c`, with a proposed alternative to `docs/dev/scripting.md`, since deleted (see `git show 5aa354c:docs/dev/scripting.md`). Findings come from reading the source and probing `./build/cmdr -c`.

## Status

Implemented:

- Step 1: `help` returns text from the active registry, whole-line `#` comments, and `lines`.

- Step 2: `$N` parameter references and parameterized `.pipeline` commands.

- Step 3: `each`, `keep`, `not`, `nonempty`, `equals`, `contains`; `cmdr_eval_args`, `cmdr_check`, and the `CMDR_MISMATCH` status.

- Step 4: `def NAME PIPELINE` and `cmdr FILE`.

- Step 5: re-evaluation of scripting.md, below. Proposals 1-4 are implemented, with the changes noted under "Implemented".

- Step 6: G1 and G7 closed; see "Closing G1 and G7".

## Findings

1. **Pipeline input binds only to the first argument.** `cmdr_eval_value` always prepends the piped value to argv.
   - `list 'x y' | map text pre` returns `x y pre`; prefixing is impossible.

   - `replace` cannot receive piped input as OLD.

   - Any command whose data argument is not first needs a C wrapper.

2. **`map` takes a command name; `try` takes a pipeline.**
   - `list a b | map 'trim | upper'` fails: `unknown command: trim | upper`.

   - `map try 'text pre'` works only because `try` re-enters the parser.

   - Both force nested quoting.

3. **`.pipeline` commands have no parameters.** `cmdr_call` rejects more than one argument, so `clean-or TEXT FALLBACK` from scripting.md requires C.

4. **Commands cannot be defined at runtime.** `.pipeline` exists only in static C tables. There is no REPL definition, script file, or rc file.

5. **`help` was not composable.** It printed to stdout and returned no value, so `help | filter list` failed. It rebuilt the registry with `cmdr_commands_init` instead of using the active one, so embedder and runtime commands were invisible. (Fixed.)

6. **Missing primitives.**
   - No `lines`; text-to-list conversion required the idiom `map text`. (Fixed.)

   - `\n` is not decoded, so `join "\n"` cannot be written in `-c` mode.

   - No predicates. `filter` is substring-only; no command fails on a mismatch, which `try`/`recover` could use for branching.

7. **No comments.** `# comment` failed with `unknown command: #`, so stdin scripts could not be annotated. (Fixed, whole-line only.)

## Proposal: three additions instead of a language

scripting.md proposes `program`/`let`/`if`/`return`, scopes, and an AST. Most of its examples need only the following.

### Typed parameter references in parsed pipeline strings

- `$1..$n` refer to the arguments of the evaluating pipeline. The parser marks them; the evaluator substitutes typed values at the argv level, never textually. A list reference stays one list argument.

- Parameters exist in `.pipeline` bodies, `try` strings, and the proposed combinators. They cannot apply to `map`, whose arguments are already evaluated.

### Pipeline-taking combinators

- `each LIST 'PIPELINE' [ARG ...]`: run the pipeline per item, with the item as `$1` and ARGs as `$2...`. This mirrors `map`, and passes outer values explicitly because strings cannot capture an enclosing body's parameters.

- `keep LIST 'PIPELINE'`: keep items whose pipeline succeeds. Success is truth, as in the shell.

- Predicates that fail on mismatch: `equals`, `contains`, `nonempty`.

### Runtime definitions

- `def NAME 'PIPELINE'` in the REPL, plus `cmdr FILE` with `#` comments.

- Definitions live in a mutable registry owned by `main.c` at the top of the parent chain. The core registry stays `const`.

scripting.md's `clean-or` example becomes:

```text
def clean-or 'trim | try "nonempty | upper" | recover $2'
```

`.pipeline` commands accept the same body today, minus `nonempty`.

Trade-off: branching is encoded as failure, and quotes still nest one level. If real programs need more than two levels of nesting, that justifies the full language.

## Decisions

- **`$1` is the first argument.** `cmdr_call` cannot tell piped input from an explicit first argument, and `map` passes items as `argv[0]`. Any other rule would make `text x | f y`, `f x y`, and `map f y` disagree.

- **The first stage receives `$1` as input unless it references `$1`.** This keeps point-free bodies (`trim | upper`) working and lets `text $2 $1` place the input explicitly. The rule is local to the first stage.

- **Arity is strict.** A body whose highest reference is `$k` requires exactly k arguments, checked before any stage runs. A body without references accepts at most one, as before.

- **Unquoted words starting with `$` are reserved.** Only `$1..$n` are valid now. `$_` (previous value) and `$name` (scripting.md bindings) are reserved and rejected. `$_` is deferred until a real example needs it. (Step 6: `$name` refers to `as` bindings; `$_` remains reserved.)

- **A bare `_` stays literal.** It is a likely separator (`split TEXT _`).

- **Out-of-range references are errors, including at the top level**, where there are no parameters. Literal fallback would hide typos. Quote (`'$5'`) or escape (`\$5`) for literal text. This breaks unquoted literal `$` words.

- **References must be whole words.** `a$1` is literal: only words starting with `$` are reserved. `$1x` is an error.

- **References cannot name commands.** Dynamic command names stay excluded.

- **No capture.** A nested string (`try "..."`) is parsed with its own parameters; it does not see the enclosing body's. Named bindings were later made visible; see "Nested strings see bindings".

- **Truth is success; false is `CMDR_MISMATCH` only.** Empty results are not false: `keep` must agree with `try`/`recover`, and `nonempty` is needed for branching anyway. A plain failure status would let a nested error (a typo inside `not`, a missing file) read as false and silently drop items. Status 2 for mismatch and 1 for error follows `grep`'s split of no-match from error.

- **One negation, `not`, instead of per-predicate inverses.**

- **`keep` and `not` validate their pipeline before running it**, so top-level errors are reported even for an empty list.

- **`not` takes a pipeline, like `each` and `keep`.** `not equals a` is an arity error; write `not "equals a"`.

- **`run` does not map exit status 1 to mismatch.** Many programs use 1 for errors; `grep`'s convention is not universal. Wrap with `try 'run ...' | recover ...` instead.

- **A `def` may shadow any command except `def`.** `help` marks definitions with `def:`. Redefining `def` would disable further definitions.

- **Definitions are one-command layers that are never moved or freed.** `cmdr_call` holds a command pointer while a body runs; a growable array could be reallocated under it by a nested `def`.

- **Bodies are checked at definition for syntax and names, not arity** (`CMDR_ANY_ARITY`). Forward references are rejected; define helpers first.

- **Names bind late.** Superseded in step 6: definitions are lexically scoped.

- **A script file stops at its first failing line**; stdin continues, as before. Later script lines may depend on earlier definitions or effects.

## Plan

1. `help` returns text and uses the active registry; `#` comments; `lines`.

2. Placeholders in the parser; parameters for `.pipeline`.

3. `each`, `keep`, and predicates.

4. `def` and `cmdr FILE`.

5. Re-evaluate scripting.md against what steps 1-4 cannot express.

## Step 5: scripting.md re-evaluated

Each example was run against `./build/cmdr` after step 4.

### Coverage

| scripting.md feature | Today | Status |
|-|-|-|
| `program f(a, b)` | `def f '...'` with `$1`, `$2` | Covered |
| Typed value references | `$N`, typed substitution | Covered |
| `if a == b` / `!=` | `equals`, `not`, `try`/`recover` | Partial: G2 |
| `return` / `else` | `recover`, `try` | Partial: G2 |
| `$lines \| sort` | none | Gap: G3 |
| `let x = ...` | helper `def` chains | Gap: G1 |
| `\n` in double quotes | `"\n"` is `n` | Gap: G4 |
| `\|` line continuation | none | Gap: G5 |
| `#` comments | whole-line only | Covered |
| Script loading | `cmdr FILE` | Covered, not atomic |
| Error locations | `FILE:LINE:` | Partial: no def name in body errors |
| Recursion limit | depth guard | Covered |

`unique-lines` needs no new syntax:

```text
def unique-lines 'read | lines | try "nonempty | sort | uniq" | recover "Empty file"'
```

### Gaps

- **G1. Two derived values cannot be combined.** Arguments are literals or parameters; no stage can compute an argument. Reporting "unique of total" needs one helper `def` per intermediate, threading values as parameters, plus an identity hack:

  ```text
  def stats2 'recover $2 $1 | sort | uniq | count | text of $1'
  def stats 'count | stats2 $1'
  read f.txt | lines | stats                          => 2 of 3
  ```

  `let` would express this directly. This is the only scripting.md feature with evidence of need.

- **G2. `try`-based branching swallows errors.** `try` captures any failure, so an error in the "then" pipeline selects the "else" value: `list a | try "nonempty | head nope" | recover Empty` returns `Empty` instead of reporting the invalid integer. `keep` and `not` avoid this with `CMDR_MISMATCH`; branching has no equivalent.

- **G3. The first-stage rule misfires on bodies that ignore `$1`.** The first stage receives `$1` unless it references `$1`, so `text $2` becomes `text $1 $2`, and `recover $2 x` above failed with a usage error. Bodies also cannot start from a parameter other than `$1`.

- **G4. Newlines cannot be written in `cmdr FILE` or `-c`.** Each file line is one pipeline, so a quoted literal newline is impossible; `join "\n"` joins with `n`.

- **G5. Long bodies must fit on one line.**

- **G6. Strict arity blocks shared arguments.** `keep nonempty b` is an arity error. Harmless for `keep`, but a combinator with several bodies sharing `$2...` (G2's fix) would reject bodies that ignore some parameters.

- **G7. Smaller gaps:** wrapping a shadowed command (`super`), numeric predicates (`lt`, `gt`), def names in body error messages, atomic file loading (a failing line leaves earlier definitions registered).

### Proposals, in order

1. **Explicit first stage (G3).** The first stage receives `$1` only if it references no parameter. Add a value stage: a first stage that is a lone `$N` starts the pipeline with that value. `swap` becomes `replace $1 $2 $3`; `stats2` becomes `$2 | sort | uniq | count | text of $1`. Breaking for step 2 bodies that reference `$2` but not `$1` in the first stage; nothing is released yet.

2. **Escapes in double quotes (G4).** Decode `\n` and `\t` inside double quotes only. Breaking: `"\n"` is `n` today. scripting.md kept the REPL unchanged, but files and `-c` share the parser and both need this.

3. **`|` continuation (G5).** In `cmdr FILE` and stdin, a line ending in `|` outside quotes continues on the next line. Keep it in `main.c`; the core parser already treats newlines as whitespace.

4. **`when VALUE COND THEN [ELSE] [ARG ...]` (G2, G6).** Run THEN on success, ELSE (default: fail with the mismatch) on `CMDR_MISMATCH`, and propagate errors. Check arity once against the highest reference across the three bodies, and let each body ignore unused parameters. `when` over `if`, because scripting.md reserves `if` as a keyword.

   ```text
   def clean-or 'trim | when nonempty upper "$2" $2'
   ```

5. **Defer `let` (G1)** until helper-`def` threading appears in real scripts. Proposal 1 removes the identity hack; what remains is naming cost and global helpers. If that is common, add `let` to def bodies only, with scripting.md's immutable, whole-token semantics.

6. **Defer G7** until a script needs one.

### Implemented

Proposals 1-4 shipped with two changes:

- **Proposal 1 kept the existing first-stage rule; only the value stage was added.** Giving the first stage `$1` only when it references no parameter would break the common predicate form `keep 'equals $2' b`, which would become `'equals $1 $2'`. The value stage alone resolves both observed failures: `$2 | sort ...` replaces the identity hack, and `"$2"` gives an ELSE that ignores `$1`. The remaining trap is a first stage such as `text $2`, which still receives `$1`; write `$2 | text`.

- **`when` requires ELSE.** An optional ELSE followed by optional ARGs is ambiguous, and a `when` without ELSE equals `COND | THEN`.

- **`when` passes each body only the parameters it references** (`cmdr_arity`), instead of relaxing arity. Strict arity still holds per body, and `when` rejects ARGs that no body references.

Verdict: the full language is not justified. Proposals 1-4 are small and close G2-G6; `let` is the only open question with evidence, and needs usage data before a decision.

## Closing G1 and G7

### G1: `as NAME` instead of `let`

Superseded: `as` is now `as VALUE NAME`, which binds a constant for the rest of the session, not a name within one pipeline. See README.md, "Blocks and definitions".

`as NAME` is a pipeline stage that binds the current value to `$NAME` and passes it through. A lone reference is a stage anywhere and replaces the current value.

```text
list b a b | as lines | count | as total | $lines | sort | uniq | count | text of $total
```

`as` over scripting.md's `let` statement: it adds no statement syntax, multi-line bodies, or scopes beyond one pipeline. Bindings are immutable, whole-word, resolved before evaluation, and not visible to nested strings, as scripting.md specified for `let`. `as` is evaluator syntax, so `def as` is rejected, and an application command named `as` is unreachable.

The first-stage rule generalizes: the current value has a name while it is `$1`, a lone reference, or just bound; a stage naming it receives it only where named. `as x | text a $x b` places the value anywhere, which replaces the reserved `$_`. `text $2` as a first stage still receives `$1`; this keeps `keep 'equals $2' b`.

### G7

- **Wrapping a shadowed command: lexical scoping for `def`.** A body resolves names in the registry as it was at definition (`.scope`). Naming its own command calls the shadowed one, and later redefinitions do not change earlier bodies. This replaces step 4's late binding. The eager name check at definition already implied it, and recursion was already impossible. `.pipeline` commands without `.scope` keep late binding.

- **Def names in body errors.** Errors from a pipeline command's stages are prefixed with its name, which gives a call chain. Arity and definition errors already name the command and are not prefixed again.

- **Numeric predicates.** `lt` and `gt`; `not` covers `>=` and `<=`.

- **Atomic file loading: not applicable.** `cmdr FILE` exits after the file, so partial definitions do not outlive a failed run, and effects cannot be rolled back. This needs a decision only if a `load` command is added.

With G1-G7 closed, scripting.md's examples are expressible without a statement language.

## Nested strings see bindings

`as` bindings are now visible to pipeline strings that a stage's command evaluates, innermost first. `$1..$N` stay local to each string, and a pipeline command's body starts without its caller's bindings.

Before, an outer value reached a nested string only as an extra argument, renumbered by the combinator. In `trim | when nonempty upper "$2" $2`, both `$2`s are FALLBACK, but the quoted one is `when`'s `$2`. Now:

```text
def clean-or '$2 | as fallback | $1 | trim | when nonempty upper "$fallback"'
```

Names over capturing `$1..$N`: every combinator already rebinds `$1` to its item, so capturing positionals would need a second syntax for outer ones. Cutting visibility at pipeline-command bodies over full dynamic scope: a body's meaning should not depend on its caller's names. A `.run_value` command that evaluates its own fixed strings also sees the bindings; only pipeline-command bodies reset them.
