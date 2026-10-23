# Paradigm and similar languages

Analysis, 2026-10-23. Status: descriptive; proposes nothing.

## Summary

cmdr's values work like a functional language's, but cmdr is not fully functional. It is an impure, applicative pipeline language with immutable values, lexical closures, and bounded iteration. The closest relatives are Tcl, Nushell, and Elixir's `|>`, not Haskell or Scheme.

## Functional traits

- **Immutable values.** Bindings are immutable and resolved before evaluation ([compose-ng.md](compose-ng.md)). There is no assignment. Binding a name again shadows it, as ML's top-level `let` does.

- **First-class lexical closures.** A block keeps the values it captured. A `def` can return a closure that runs later ([blocks.md](blocks.md)). A block passed as a parameter can be called:

    ```text
    cmdr> def app { (f x) with $x $f }
    cmdr> app { (y) mul $y 2 } 21
    42
    ```

- **Combinators over loops.** Iteration uses `each`, `keep`, `fold`, `repeat`, and `retry`. A condition loop would need state that changes between iterations, and cmdr values have none ([missing-syntax.md](missing-syntax.md)).

- **Expression-oriented control flow.** `if` and `try` are commands that return values. `try`'s handler is a block that receives the error message.

- **Lexically scoped definitions.** A `def` body uses the commands that existed when it was defined. Later definitions do not change it.

## Missing functional traits

- **Purity.** `write`, `run`, `fs.remove`, and `random.*` are ordinary commands, callable at any stage. "Explicit effects" means programs run only through `run`. Effects are not tracked or isolated, and a stage's effects remain if a later stage fails.

- **Recursion.** A `def` cannot call itself: naming its own command calls the one it shadows. Self-application can be written, but stops at `MAX_CALL_DEPTH = 128` in `src/cmdr.c`:

    ```text
    cmdr> def w { (f) with $f $f }
    cmdr> w { (g) with $g $g }
    cmdr: w: command 'with' is nested too deeply (possible cycle in composed commands)
    ```

    `fold`, `repeat N`, and `retry N` are bounded. Without `run`, cmdr is therefore not Turing-complete (inference; not proven).

- **Function composition.** `|` sequences stages within one pipeline. No operator composes two blocks into a new block value.

- **Data types.** Values are text, lists, or no value. There are no records, algebraic data types, or pattern matching. Numbers are text.

## Framings

| Framing | Shared traits | Difference |
|-|-|-|
| Dataflow / pipeline language: Elixir `\|>`, Clojure `->`, Nushell | Linear chains; the previous result is the first argument | Nushell has structured records and tables |
| Tcl-family command language | Every line is `command word...`; blocks are code values; `if` and `try` are shadowable commands | Tcl re-parses strings; cmdr never re-parses values |
| Total functional language: Dhall, Charity | Closures, immutability, bounded iteration, no general recursion | Dhall has no side effects; cmdr's are unrestricted |

## Comparison

| | cmdr | Tcl 8.6 | Nushell | Dhall |
|-|-|-|-|-|
| Values | Text and lists; numbers are text | Every value is a string; lists and dicts are string forms | Structured: ints, records, tables, and more | Typed: `Natural`, `Text`, records, unions, lists |
| Mutation | None; rebinding shadows | `set` mutates variables | Immutable by default; `mut` declares a mutable variable | None |
| Re-parsing | Never | `eval`, `subst`, and `expr` re-parse strings | No word splitting of values | None |
| Closures | Capture values | `apply` lambdas capture nothing; `upvar` reaches caller frames | Capture values; cannot capture `mut` variables | All functions are lambdas |
| Recursion | None; call depth stops at 128 | Yes; `interp recursionlimit` defaults to 1000 | Yes, in custom commands | None; programs always terminate |
| Side effects | Unrestricted commands | Unrestricted | Unrestricted | None; imports are the only I/O, resolved before evaluation |
| External programs | Only through `run` and its variants | `exec` | A bare name runs one; `^name` forces it | None |
| Pipelines | `\|` passes the value as the first argument | None in the language; `exec` builds OS pipes | `\|` passes the value as input (`$in`); output can stream | None |
| Checks before running | Syntax, names, references, block arity, per line | None; a command is parsed when it runs | Whole script parsed; optional parameter types checked | Full static type check |
| Control flow | Commands, shadowable by `def` | Commands, replaceable with `rename` | Parser keywords (unverified whether a `def` can shadow `if`) | `if then else` expression; `merge` over unions |
| Errors | Mismatch vs error; `try`, `or`, `check` | `catch`; `try`/`trap` | `try`/`catch`; errors are values | No exceptions; failure is a type such as `Optional` |

Sources: Tcl [apply](https://www.tcl.tk/man/tcl8.6/TclCmd/apply.htm), [interp](https://www.tcl.tk/man/tcl8.6/TclCmd/interp.htm), [try](https://www.tcl.tk/man/tcl8.6/TclCmd/try.htm); Nushell [variables](https://www.nushell.sh/book/variables.html), [types of data](https://www.nushell.sh/book/types_of_data.html), [pipelines](https://www.nushell.sh/book/pipelines.html); Dhall [safety guarantees](https://docs.dhall-lang.org/discussions/Safety-guarantees.html). The non-cmdr columns are written from knowledge of these docs. They were not re-checked against the current versions or tested.

Two conclusions follow:

- cmdr shares Dhall's restrictions on mutation and recursion, but not its purity or types.

- cmdr shares Nushell's model of passing values through pipelines, but not its structured data or streaming. cmdr's `run` returns stdout only after the program exits.

## Open questions

- Is the absence of recursion intended as a termination guarantee? If so, it could be documented as one. If not, the 128-depth guard is an incidental limit.
