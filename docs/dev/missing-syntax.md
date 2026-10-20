# Missing syntax

Design exploration, 2026-10-20. Status: proposed; nothing here is implemented.

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
if $key { equals . } { (_) list rest $t $length | unwords } { (k) item note $t $length $k 100 1 }

# A condition that ignores the value it tests.
if $n { (_) random | lt $p } { (m) $m } { (m) to-rest $m }
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
if [VALUE] COND THEN [ELSE]
```

- COND runs with VALUE. On success, THEN runs with VALUE. On a mismatch, ELSE runs with VALUE; without ELSE, `if` returns the mismatch.
- An error in COND propagates. `if` never treats an error as false, unlike `try`.
- Only the chosen branch runs.
- All three blocks have their arity checked before COND runs, so a typo in a branch not taken still fails.
- VALUE may be piped: `... | if { fs.is-file } { ... } { ... }`.

`if` is a command, not syntax, so a `def` can shadow it like any other command.

### Use it

```text
# midi.seq: a rest or a note, without computing the note for a rest.
if $key { equals . } { (_) list rest $t $length | unwords } else { (k) item note $t $length $k $velocity $channel }

# Render a file once: a recipe that runs only if its output is missing.
at a440.wav | if { fs.is-file } { (p) $p } else { (p) snd.tone 2 440 | snd.out $p }

# midi.chance: a note becomes a rest with probability 1 - P; other items pass.
def to-rest { (n) list rest $(field $n 2) $(field $n 3) | unwords }
if $n { (m) random | ge $p | $m | matches "^note " } { (m) to-rest $m } else { (m) $m }

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
- Quoting hides it: `'$(x)'` is literal text. A `)` inside it closes it unless quoted or part of a nested `$(...)`.

`$(` is an invalid reference today, so no script changes meaning. Plain `(...)` was rejected: `text (a)` prints `(a)` today, and would change. Shell has the same form, `$(...)`.

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

## Cost

- **`_`:** the parameter parser and the unused check. No API change.
- **`if`:** one primitive, about 40 lines, beside `try` and `or`. No syntax change.
- **`$(...)`:**
  - The lexer reads a balanced `$(...)`.
  - The compiler compiles it like a block body with no parameters.
  - The evaluator runs it before the stage's call.
  - The editor's scanner colors it and its contents.
  - No API change.
