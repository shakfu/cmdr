# `$name` for session bindings

Design, 2026-10-20. Status: implemented, without the editor change below.

## Problem

`as VALUE NAME` makes NAME a command that returns VALUE. A command runs only as the first word of a stage, so a binding cannot be an argument:

```text
text a.wav | as a
text b.wav | as b
list a b                                         # the words a and b
a | with { (x) b | with { (y) list $x $y } }     # the values, one with per name
```

Python evaluates a name wherever it appears: `[a, b]`. cmdr has one syntax for a value in argument position, `$name`, and today it names only block parameters.

## Proposal

`$name` also refers to a session binding when no enclosing block declares `name`.

```text
list $a $b                     # a.wav b.wav
snd.out $chain $out-file
$tones | report                # a lone reference is a value stage, as today
$tones.hz                      # a module's binding, by its qualified name
```

## Semantics

1. **Resolution order: parameters, then bindings.** The innermost declaring block wins, then the bindings visible where the line compiled. A parameter shadows a binding of the same name, as an inner parameter already shadows an outer one.
2. **At compile time, as names are checked now.** A reference to a binding resolves when its line or `def` compiles. A `def` keeps the value it saw, the rule `as` already has. The line that binds a name cannot refer to it: `text x | as y | text $y` fails before anything runs, as `| y` does.
3. **Values only.** `$upper` or `$some-def` fails: `invalid reference '$upper': upper is a command, not a value; call it as a stage`. `$` never runs code, so a reference cannot have effects or fail at run time.
4. **Qualified names.** A reference may contain `.`, for `$MODULE.NAME`. Parameter names still may not. `$x.y` is an error today, so this breaks nothing.
5. **Unchanged:** quoting (`'$a'`, `a$x` and `"$a"` stay literal), the unused-parameter rule, and value stages.

## Mechanism

- **A constant is a command with a value.** `cmdr_command` gains `const cmdr_value *value`; a command sets one of `run`, `block`, or `value`. `cmdr_call` returns a copy of `value` and rejects arguments. `as` sets `value` to its node's copy, replacing `constant_command`. A host can declare constants in its own table: `{.name = "pi", .value = &pi}`.
- **Compile.** `compile_body` tries the scope's parameters, then `cmdr_find(repl, name)`. A command with `value` makes the word point at it; any other command is error 3.
- **Evaluate.** A word that points at a constant passes that value, borrowed, as a parameter's slot is. Session nodes live until close and a host owns its table, so the pointer outlives every program compiled against it.
- **API.** `cmdr_session_binding`, added with `as` and unreleased, is removed: `command->value` answers the same question for any table.
- **Editor: not done.** Coloring `$name` by what it resolves to needs the scanner to track each open block's parameters, so a parameter that shadows a binding stays plain. All references stay magenta.

## Alternatives

| Alternative | Rejected because |
|-|-|
| Resolve at run time, by name | A `def` would see later rebindings: dynamic scope, against `as`'s rule that a definition keeps the value it saw. Errors would move from compile time to run time. |
| `$name` runs any zero-argument command, such as `$time-today` | `$` would run code, so a reference could have effects or fail mid-pipeline. A computed value is a stage: `time-today \| with { (t) ... }`. |
| A second sigil for bindings, such as `@a` | Two syntaxes for "a value here", and a block could not shadow a binding. Python has one kind of name. |
| Copy the value into each compiled program | Copies on every compile, for no gain: the value is constant and outlives the program. |
| Keep `$` for parameters only | `list a b` stays a trap, and every combination of bindings needs one `with` per name. |

## Cost

- **ABI:** `cmdr_command` grows a field. Before 1.0 a minor version may break the API (README, "Install"); hosts that use designated initializers recompile unchanged.
- **Reading code:** `$x` in a block now has two possible meanings. The editor's colours tell them apart.

## Tests

- `$binding` as an argument, a value stage, and inside nested blocks.
- A parameter shadows a binding.
- A `def` keeps the value it saw after rebinding.
- `$binding` on the line that binds it fails at compile time.
- `$command` gives error 3, and `$MODULE.NAME` works after `import`.
- `test_api.c`: a host table constant, `.value`, as a stage and as `$name`.
