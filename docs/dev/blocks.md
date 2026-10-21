# Blocks and parameter scope

Decision record, 2026-10-17. Three prototypes were built against `836ade2`, outside the tree. Each passed the 23 existing tests plus its own block tests, under ASan and UBSan.

## Decision

- A block literal `{ ... }` is a value. Its syntax and command names are checked with the enclosing line, before any stage runs.

- A block declares its parameters: `{ (x d) text $d $x $d }`. A block that names its input must declare it.

- Block arity is exact. `{ ... }` takes one input, received only by pipe. `{ () ... }` takes none. `{ (a b) ... }` takes two. See [Piping](#piping).

- Declared parameters appear only where named, and each must be used. Only `{ ... }` pipes its input into its first stage.

- `$name` resolves lexically, through nested blocks, innermost first. A block captures the names visible where it is written.

- `def NAME BLOCK` defines a command.

- Positional `$N` and string bodies are removed.

- Later: a parameter after the first may have a default, so arity is a range. See [defaults.md](defaults.md).

`{ }` over `[ ]`: three existing tests use `text [ $1 ]` as literal text.

Parameters inside the block over `def wrap (x d) { ... }`: blocks passed to `each`, `keep`, `not`, and `try` use the same form. The line compiles before `def` runs, so a list outside the block would need a parser rule that joins it to the next block.

## Variants

| | `$N` in a block refers to | Block input |
|-|-|-|
| P | the block's own call arguments | `$1` |
| L | the enclosing body's arguments, captured | `$0` |
| N | a declared name in any enclosing block | the first declared name |

Prototype size, C lines (`836ade2`: 1308):

| P | L | N |
|-|-|-|
| 1344 | 1403 | 1541 |

N still carried positional `$N` and string bodies. Removing them did not make the shipped form smaller: it has 1463 lines, and 1473 with
[compilation](#compilation).

## Side by side

Results are actual output.

| # | P | L | N |
|-|-|-|-|
| 3 | `def without { keep { not { equals $2 } $2 } $2 }` | `def without 'keep { not { equals $2 } }'` | `def without { (xs v) keep { not { equals $v } } }` |
| 5 | `each { split , \| each { text $1 in $2 } $1 \| join ';' }` | needs a helper def | `each { (r) split , \| map { (f) text $f in $r } \| join ';' }` |
| 7 | `def suffix { each { text $1 $2 } $2 }` | `def suffix 'each { text $0 $2 }'` | `def suffix { (xs t) each { (s) text $s $t } }` |
| 8 | fails: no capture | `15` | `15` |
| 9 | `keep { gt $2 \| lt $3 } $2 $3` | `keep { gt $2 \| lt $3 }` | `keep { gt $lo \| lt $hi }` |
| E2 | error | `no`: missing input became the fallback | `try: block takes 1 argument, got 0` |
| E3 | `reference $3 has no argument` | typo `$3` raised the def's arity to 3 | `invalid reference '$u'` before any item runs |

Scenarios: 3 filters by a def argument; 5 uses the outer item in an inner block; 7 suffixes each item; 8 returns a closure from a def and calls it later; 9 filters by a range given as arguments; E2 runs `try` on a block that needs input, without input; E3 contains a typo in a reference.

## Findings

- P repeats each outer value at every nesting level. It cannot return a closure.

- L's costs come from positional `$N` and `$0`, not from lexical scope:
  - `def NAME { }` cannot work, because the block's `$N` bind to the top-level line, which has no arguments.

  - A top-level block cannot take a value.

  - An inner block cannot reach an outer block's `$0`.

  - A missing input is replaced by `try`'s fallback (E2).

  - A typo changes a def's arity (E3).

- N fixes all five. It costs a declaration on blocks that name their input.

- L and N pass a block the caller's values without copying them. This is safe only while commands copy, rather than keep, the arguments they receive.

## Rejected

- Up-level references (`$^N`): the caret count depends on nesting depth, so moving a block changes its references.

- Explicit binding (`bind BLOCK ARG...` on P): one rule, but outer values are still passed by hand.

- Dynamic scope: a block passed into a def would see that def's arguments. This makes static checks impossible.

## Piping

The first rule: a stage received the current value as its first argument unless the stage named that value. Naming it anywhere in the stage, including as a fallback, suppressed the pipe:

```text
cmdr> list 1 x 3 | each { (n) try { add 1 } $n } | join ,
1,x,3
cmdr> list 1 x 3 | each { (n) try $n { add 1 } $n } | join ,
2,x,4
```

The first form calls the two-argument `try BODY FALLBACK`. `add 1` then fails for lack of input, and `try` returns the fallback. The result is wrong, with no error.

Exact block arity turns this into an error before `try` runs: `try: block takes 1 argument, got 0`. A body that takes no input says so: `try { () div 1 0 } x`.

The rule had a second silent case. A block could not start from anything but its input, because its first stage always received it:

```text
cmdr> text ERROR | try { (w) read app.log | contains $w } no
no
```

`read` received `ERROR` and `app.log`, failed with a usage error, and `try` returned the fallback.

Adopted: a declared parameter appears only where named. Only `{ ... }` pipes its input into its first stage. This removes the exception from the piping rule: each stage receives the previous result first. The example above returns the log text.

A declared parameter that is never used is an error, because it would never receive its argument. Otherwise `text a | try { (s) upper } no` would return `no`: `upper` runs without input, fails, and `try` hides the failure. Now it fails with `parameter $s is never used`, before any stage runs.

Costs:

- A declared block whose first stage uses its input must name it: `{ (xs v) keep $xs { ... } }`. Nine test sources changed.

- A block cannot ignore an argument it receives.

- A value stage is always piped onward: `$s | text $s` gives `a a`, not `a`.

## Compilation

A line compiles once, before any stage runs, and its nested blocks compile with it. Compiling parses the source, checks command names, and replaces each `$name` with its index in the environment. At run time the environment is a plain array of values: a block's captured values, then its arguments.

A block value points at its compiled code, which keeps a reference count. Copies share the code, and the last copy frees it. `def` keeps a copy, so a defined command is never parsed again.

Reference counting over caching the compiled code per call of `each`: a block can outlive the line that compiled it. `adder` returns a closure that runs after the def's body has finished.

Before, every call re-parsed the block, and every block nested in it. 20000 items through `each`, measured:

| Body | Per call | Once |
|-|-|-|
| `{ (s) text $s ! }` | 0.023 s | 0.010 s |
| 8 nested blocks | 0.705 s | 0.074 s |

Composed commands are `def`s in `src/prelude.cmdr`, which the build embeds and cmdr runs at startup. This replaced `.pipeline` entries in C tables over keeping both. A `const` table entry could not hold its compiled code, so it compiled once per call. With `.scope` unset, it also resolved names in the caller's scope, so a user's `def tail` could change a prelude command. A `def` has neither problem.
