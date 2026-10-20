# Missing syntax

Design exploration, 2026-10-20. Status: implemented, except structured data, which TODO.md tracks.

cmdr is a glue language: pipelines of commands over text and lists. Three gaps cost the most code in this repository's own scripts. Each section states the problem with counts, the proposal, examples rewritten from real files, and when not to use the feature.

| Proposal | Fixes | Size |
|-|-|-|
| [`_`](#_-an-ignored-parameter) | Blocks that must declare a parameter they do not need | Small: the parameter parser and the unused check |
| [`if`](#if-a-conditional-that-runs-one-branch) | Conditionals whose fallback is computed even when not chosen | Small: one primitive |
| [`$(...)`](#-a-sub-pipeline-as-an-argument) | One `with` per computed argument | Medium: lexer, compiler, evaluator, editor |

Recommended order: `_`, then `if`, then `$(...)`. `if`'s branches use `_`; `$(...)` is independent and the largest.

## `_`: an ignored parameter

### Problem

A block must use every parameter it declares (blocks.md). The rule catches a real bug: `text a | try { (s) upper } no` returned `no`, because `upper` ran without input. But some callers pass an argument the block does not need:

- `each` passes the item to a block that only repeats an action. `repeat` was added for this case.
- A branch of `if`, below, receives the value it was chosen for, whether it needs it or not.
- `midi.chance` draws a random number, tests it, then switches to the item with a lone `$n` stage. The draw never uses the item.

### Proposal

`_` in a parameter list declares a parameter that is ignored.

- It may appear more than once: `{ (_ _ x) ... }`.
- It is exempt from the unused-parameter rule.
- `$_` is an error: `nothing is bound to _`.
- It takes no default.

`_` is an invalid parameter name today, so no script changes meaning. Python uses `_`, by convention, for a value it ignores.

### Use it

```text
# Eight draws, one per item; the item is not needed.
num.range 8 | each { (_) random.int 1 6 }

# A branch that does not need the value it was chosen for (see if).
if $key { equals . } { (_) list rest $t $length | unwords } else { (k) item note $t $length $k 100 1 }

# A condition that ignores the value it tests.
if $n { (_) random.random | lt $p } { (m) $m } else { (m) to-rest $m }
```

### Don't use it

- **To silence an error about a parameter you meant to use.** That error means the block ignores its argument, usually by mistake. Fix the block.

- **When there is no list to iterate.** `repeat 8 { () random.int 1 6 }` states the intent; `num.range 8 | each { (_) ... }` makes a list only to discard it.

- **When the block uses its input implicitly.** `{ upper }` takes its piped input; it needs no parameter at all.

## `if`: a conditional that runs one branch

### Problem

`or`, `try`, and `check` take a fallback value, which is computed before the choice. A branch that must not run, because it fails or has effects, needs a workaround:

- `midi.seq` (examples/midi/midi/__init__.cmdr) computes the note for every key, including the rest marker `.`, which `midi.key` rejects. It substitutes `0` first, then discards the note:

  ```text
  with { (r) or { () equals $key . | replace . 0 } $key | with { (safe) item note $t $length $safe $velocity $channel } | with { (n) or { () equals $key . | $r } $n } }
  ```

- Rendering a file only if it is missing needs an `each` over a list of 0 or 1 items.

compose-ng.md proposed this command as `when`, because scripting.md reserved `if` as a keyword. scripting.md is deleted, and Python's word is `if`.

### Proposal

```text
if [VALUE] COND THEN [elif COND THEN ...] [else ELSE]
```

- Each COND runs with VALUE, in order. The THEN of the first that holds runs with VALUE. If none holds, ELSE runs with VALUE; without `else`, `if` returns the last mismatch.
- `elif` and `else` are words, as `as` is in `import`. They count only after a THEN, so a VALUE may be the word `else`. A block where `elif` or `else` belongs is an error; there is no positional ELSE.
- An error in COND propagates. `if` never treats an error as false, unlike `try`.
- Only the chosen branch runs.
- Every block has its arity checked before the first COND runs, so a typo in a branch not taken still fails.
- VALUE may be piped: `... | if { fs.is-file } { ... } else { ... }`. It is present when the arguments only parse with it, so a block can be a VALUE.

`if` is a command, not syntax, so a `def` can shadow it like any other command.

### Use it

```text
# midi.seq: a rest or a note, without computing the note for a rest.
if $key { equals . } { (_) list rest $t $length | unwords } else { (k) item note $t $length $k $velocity $channel }

# Render a file once: a recipe that runs only if its output is missing.
at a440.wav | if { fs.is-file } { (p) $p } else { (p) snd.tone 2 440 | snd.out $p }

# midi.chance: a note becomes a rest with probability 1 - P; other items pass.
def to-rest { (n) list rest $(field $n 2) $(field $n 3) | unwords }
if $n { (m) random.random | ge $p | $m | matches "^note " } { (m) to-rest $m } else { (m) $m }

# A guard with no ELSE: the mismatch is the result, so keep drops the item.
list a.wav b.txt | keep { if { ends-with .wav } { (f) fs.is-file $f } }
```

### Don't use it

- **When the fallback is a cheap constant.** `or $x { nonempty } none` is shorter. `midi.rotate`'s fallback is its input; `or` is right there.
- **To filter a list.** Use `keep`.
- **To replace a failure message.** Use `check`.
- **To recover from an error,** such as a missing file. `if` lets errors through by design; use `try`.

## `$(...)`: a sub-pipeline as an argument

### Problem

An argument is a literal, a `$name`, or a block. A computed argument needs a `with` to bind it first:

```text
def write-to { (chain p) at $p | with { (o) snd.out $chain $o } }
```

The repository's `.cmdr` files hold 430 `with { (x) ... }` blocks. In 372 of them (87%), the parameter is used exactly once, so the `with` exists only to move one computed value into an argument position. `$name` for bindings (bindings.md) applied to 15 of them, 3%. Single-use `with` blocks are the larger part.

### Proposal

`$(PIPELINE)` is an argument whose value is PIPELINE's result.

- It runs when its stage runs, before the stage's command, left to right among the arguments.
- It receives no input, as a block with `()` does.
- It sees the enclosing blocks' parameters and the session's bindings, as any word in its place would.
- It compiles with its line, so its names and syntax are checked before anything runs.
- Its result is one argument; a list stays one argument. No value is an error: `$(...) returned no value`.
- A failure or mismatch inside it fails the stage, with the same status.
- It cannot be a command name, as a reference cannot. A whole stage of `$(...)` is an error; write the pipeline itself.
- Quoting hides it: `'$(x)'` is literal text. Unquoted parentheses inside it balance, as braces do in a block, so `$(text (a))` keeps its parentheses. The proposal had the first unquoted `)` close it, which would have broken `text (a)` there.

`$(` is an invalid reference today, so no script changes meaning. Plain `(...)` was rejected: `text (a)` prints `(a)` today, and would change. Shell has the same form, `$(...)`.

### Relation to blocks and with

- A block is a lambda: `{ (x) upper $x }` is a value that runs later, when `each`, `if`, or `with` calls it, as often as they call it.
- `$(PIPELINE)` runs once, where it is written, and takes no arguments. It is an immediately applied lambda with no parameters, Python's `(lambda: sum(xs))()`. It sees the enclosing names, as a closure does.
- `A | with { (v) BODY }` is a lambda applied to A: `(lambda v: BODY)(A)`, a `let`. Replacing it with BODY, with `$(A)` in place of `$v`, inlines a let-bound value.

Inlining keeps the meaning only if A is computed once, at the same point. So the conversion skipped a `with` when the following held. `scripts/flatten_with.py` automated it until it was removed after 0.2.0; see that tag.

- `$v` occurs more than once: each `$(A)` runs A again.
- `$v` occurs inside a nested block: the block may run many times, and A with it.
- A has effects or random draws, or takes the pipeline's input: moving it changes their order, or cuts it off from the input.

### Use it

Each before is a line from this repository; the after is equivalent.

```text
# src/prelude.cmdr: nth
before: gt $n 0 | $xs | count | with { (c) le $n $c } | $xs | head $n | last
after:  gt $n 0 | le $(count $xs) | $xs | head $n | last

# examples/sox-dsl/demo.cmdr: write-to
before: def write-to { (chain p) at $p | with { (o) snd.out $chain $o } }
after:  def write-to { (chain p) snd.out $chain $(at $p) }

# examples/sox-dsl/demo.cmdr: report's line per file
before: snd.info $p | head 3 | rest | unwords | with { (i) path-basename $p | with { (b) list $b $i | unwords } }
after:  list $(path-basename $p) $(snd.info $p | head 3 | rest | unwords) | unwords

# examples/agent-workflows/aw/__init__.cmdr: action-file
before: list $a .md | concat | with { (f) home-dir | path-join actions | path-join $f }
after:  path-join $home-dir actions | path-join $(list $a .md | concat)

# examples/midi/demo.cmdr: the song
before: drums | midi.loop 4 | with { (d) melody | midi.loop 2 | midi.stack $d } | midi.stack $bass | midi.stack $pads | with { (s) midi.tempo 80 | midi.shift 12 | midi.stack $s } | ...
after:  drums | midi.loop 4 | midi.stack $(melody | midi.loop 2) | midi.stack $bass | midi.stack $pads | midi.stack $(midi.tempo 80 | midi.shift 12) | ...
```

### Don't use it

- **When the value is used twice.** `$(...)` runs once per occurrence. Two `$(random)` are two draws, and two `$(read f)` read twice. Bind it once with `with` or `as`:

  ```text
  words $n | with { (w) head $w $(sub $i 1) | push $v | append $(tail $w $(sub $(count $w) $i)) }
  ```

  Here `$w` is used three times, so the `with` stays.

- **For the main flow of data.** A value transformed step by step reads left to right as a pipeline: `read f | trim | upper`, not `upper $(trim $(read f))`.
- **For effects.** `$(run ...)` or `$(write ...)` hides an effect inside an argument. Put effects in stages, where their order is visible.
- **More than one level deep.** `$(sub $(count $w) $i)` above is at the limit. Deeper nesting reads worse than the `with` it replaces; use a `with` or a `def`.

### Alternatives

| Alternative | Rejected because |
|-|-|
| `(PIPELINE)` | Changes the meaning of existing text such as `(a)` |
| Backticks, as old shells | No nesting without escapes; shells moved to `$(...)` for this reason |
| An `as NAME` stage that binds within one pipeline (compose-ng.md, G1) | Names a value but still needs a stage per value; `$(...)` puts the computation where it is used |
| Keep `with` | 372 single-use `with` blocks stay |

## Library gaps

The syntax above covers how a script composes. Three gaps remain in the commands, by the same count of this repository's scripts. A fourth, structured data, is a decision about the value model; see TODO.md.

| Proposal | Fixes | Size |
|-|-|-|
| [`concat VALUE ...`](#concat-joining-text-without-list) | Building text with `list ... \| concat` | Small: one primitive replaces a prelude definition |
| [`try`'s handler](#try-with-a-handler-block) | A fallback that cannot see why the block failed | Small: a branch in `try` |
| [`retry`](#retry-attempts-until-success) | Polling and retries, which have no loop | Small: one primitive |

### `concat`: joining text without `list`

#### Problem

`concat` joins one list's items, so text built from parts first collects them with `list`. The scripts hold 69 `list ... | concat`:

```text
list "### " $f "\n```\n" $c "\n```" | concat
list "failed with status " $s ":\n" $e | concat
```

#### Proposal

`concat VALUE ...` joins its arguments without a separator. A text argument adds itself; a list adds its items. So `list a b | concat` keeps its meaning, and the line above becomes:

```text
concat "### " $f "\n```\n" $c "\n```"
```

It is Python's `"".join`, over all arguments. A primitive replaces the prelude's `def concat { join "" }`.

`format TEMPLATE ARG ...`, as Python's `str.format`, was the alternative, and was added beside `concat`. `concat` adds no syntax and suits joining parts. `format` keeps the result's shape in one string, so it suits a sentence, `format "{} has {} lines." $f $n`, where `concat` cannot put punctuation directly after a `$(...)`, and a template kept as data. Its fields are `{}` and `{N}`, with `{{` and `}}` for braces; an unused argument is an error, unlike Python.

#### Use it

```text
concat cli- $n
concat "sandbox unavailable: " $e
concat $base $(by-mode $mode "" $llama-format)
```

#### Don't use it

- **To join with a separator.** Use `join LIST SEP`, or `text`, which separates with spaces.
- **On a list whose items are lists.** An item must be text; `concat` fails otherwise, as `join` does.

### `try` with a handler block

#### Problem

`try [INPUT] BLOCK FALLBACK` returns FALLBACK on any failure, and the reason is lost. The scripts hold 39 `try`. `aw`'s `edit-stage` replaces every parse failure with one fixed message, though `edit-parse` reports which part was missing:

```text
list "" "" "" "malformed edit block: expected a file path alone on a line, SEARCH, =======, and REPLACE" | with { (bad) try $c { (x) edit-parse $d $x } $bad }
```

#### Proposal

If FALLBACK is a block, `try` calls it with the error message, without cmdr's `cmdr: ` prefix, and returns its result. Python's `except E as e`. A FALLBACK that is text or a list is returned as now. The handler's arity is checked with BLOCK's, before either runs. An error in the handler propagates. An interrupt is still not caught.

#### Use it

```text
try $c { (x) edit-parse $d $x } { (e) list "" "" "" $(concat "malformed edit block: " $e) }
try { () read config.txt } { (e) concat "# defaults: " $e }
```

#### Don't use it

- **When the reason does not matter.** `try { read } ""` is shorter.
- **To branch on a predicate.** A mismatch is not an error; use `if` or `or`, which let errors through.

### `retry`: attempts until success

#### Problem

A `def` cannot call itself, and `repeat` runs a fixed number of times, so "until it succeeds" cannot be written. `aw`'s llama adapter retries once by hand; a script that polls a server or a file cannot poll at all.

#### Proposal

`retry N [every SECONDS] BLOCK [ARG ...]` calls BLOCK with the ARGs until a call succeeds, at most N times, and returns that call's result. With `every`, it waits SECONDS between attempts, not before the first; an interrupt ends the wait. If every call fails, it fails with the last failure, its status kept, so a mismatch stays a mismatch. Each failure counts: an error or a mismatch. An interrupt stops it. N is 1 to 10,000,000, as for `repeat`.

`retry` over `until COND` or `while COND`: a loop over a condition needs state that changes between iterations, which cmdr values do not have; an attempt that can fail is what scripts need. N bounds it, so it cannot run forever as recursion could.

The wait is part of `retry`, not of BLOCK, because a block cannot run two commands in turn without piping one into the next: `run sleep 1 | run curl ...` passes sleep's output to `run` as its program. `every` is a word, as `else` is in `if`.

#### Use it

```text
# Wait for a server: up to 30 attempts, a second apart.
retry 30 every 1 { () run curl -sf http://localhost:8080/health }

# Until a file appears: a mismatch from fs.is-file is a failed attempt.
retry 50 every 0.2 { () fs.is-file out.json }
```

#### Don't use it

- **For a fixed number of independent calls.** Use `repeat`, which keeps every result.
- **Around an effect that is not safe to repeat,** such as `append`. Each attempt repeats the effects before its failure.

## Cost

- **`_`:** the parameter parser and the unused check. No API change.
- **`if`:** one primitive, about 40 lines, beside `try` and `or`. No syntax change.
- **`$(...)`:**
  - The lexer reads a balanced `$(...)`.
  - The compiler compiles it like a block body with no parameters.
  - The evaluator runs it before the stage's call.
  - The editor's scanner colors it and its contents.
  - No API change.
