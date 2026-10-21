# Numbers, booleans, null, and dicts

Design sketch, 2026-10-21. Status: nothing implemented. Examples show intended output, not current output.

## Problem

Values are text, lists, and blocks (`include/cmdr.h:11`). A number is text that arithmetic parses, and truth is a predicate's mismatch status. Three things follow:

- `list 10 9 2 | sort` gives `10 2 9`, by byte order.

- `equals 1.0 1` mismatches.

- Truth cannot be stored: `list 1 2 | each { (x) gt $x 1 }` fails on the first item.

The target is JSON's value set: null, bool, number, text, list, and dict. The sketch follows Python unless cmdr's text-first inputs give a reason not to. The exceptions are listed in [Departures from Python](#departures-from-python).

## Values

| Kind | Python | Example |
|-|-|-|
| text | `str` | `hello`, `"1.10"` |
| number | `float` | `3`, `-1.5e3` |
| bool | `bool` | `true`, `false` |
| null | `None` | `null` |
| list | `list` | `$(list 1 a true)` |
| dict | `dict` | `$(dict name cmdr stars 3)` |
| block | function | `{ (x) add $x 1 }` |

A number is a finite double. Lists and dicts may mix kinds and nest. Null is a value, unlike the no-value result of `def` and `as`: a list can hold it, and `$(...)` can return it.

## Literals

An unquoted word is a number if it matches Python's decimal literal grammar without `_`: `3`, `-1.5`, `2e3`. A quoted word is text.

- `007` is text, since Python rejects leading zeros in an integer literal. Zip codes and file names keep their form.

- Hex, octal, `inf`, and `nan` are text.

- `true` and `false` unquoted are bools, and `null` is null. Quoted, each is text.

- A number keeps its source spelling until arithmetic makes a new value. `text 1.10` prints `1.10`.

```text
cmdr> list 1.10 007 "3" true | each { type }
number
text
text
bool
```

`lines`, `split`, `read`, and `run` return text, as Python's `str.split` and `subprocess` do.

## Conversion

| cmdr | Python | Rule |
|-|-|-|
| `number VALUE` | `float(x)` | Parse text by the literal grammar; return a number unchanged; error on bool |
| `text VALUE ...` | `str(x)` | Spelling of each argument, joined by spaces |
| `bool VALUE` | none | `true` or `false` as text or bool; anything else is an error |
| `type VALUE` | `type(x).__name__` | `text`, `number`, `bool`, `null`, `list`, `dict`, or `block` |

`bool "false"` is `false`. Python's `bool("false")` is `True`, a truthiness rule cmdr does not have.

## Arithmetic and comparison

`add`, `sub`, `mul`, `div`, `lt`, `gt`, `le`, `ge`, and `num.*` take numbers, and text that parses as one. They return numbers.

```text
cmdr> read count.txt | add 1
4
cmdr> add true 1
cmdr: add: true is a bool, not a number
```

Division by zero and overflow stay errors, as Python's `ZeroDivisionError` and `OverflowError`.

## Equality

| Command | Compares | `1.0` vs `1` | `"3"` vs `3` | `true` vs `"true"` |
|-|-|-|-|-|
| `equals A B` | spellings | mismatch | holds | holds |
| `eq A B`, `ne A B` | numbers, parsing text | holds | holds | error |

`equals` is unchanged. `eq` is Python's `==` on floats.

## Ordering

`sort` follows `sorted`:

- All numbers: numeric order.

- All text: byte order.

- Mixed, or any bool: an error, as Python's `TypeError`.

- `sort VALUE KEY` sorts by a key block, as `sorted(key=...)`. `sort-n` becomes `sort { number }`.

```text
cmdr> list 10 9 2 | sort | join ,
2,9,10
cmdr> read sizes.txt | lines | sort { number } | join ,
2,9,10
```

## Booleans and mismatch

Predicates keep their contract: return the input, or mismatch. `if`, `keep`, `not`, `or`, and `check` still take blocks. Two commands convert between the two forms of truth:

- `test [INPUT] BLOCK` returns `true` if the block holds, `false` if it mismatches. Errors propagate.

- `is VALUE` returns `true` if VALUE is `true`, and mismatches if it is `false`. Any other value is an error.

```text
cmdr> list 1 2 3 | each { test { gt 1 } } | join ,
false,true,true
cmdr> as true verbose
cmdr> if { is $verbose } { text loud } else { text quiet }
loud
```

There is no truthiness. `is ""` and `is 0` are errors, where Python's `if "":` is false.

## Null

`is-null VALUE` returns VALUE if it is null, and mismatches otherwise, as Python's `x is None`. Text commands and `equals` reject null, so it cannot become the text `null` by accident.

```text
cmdr> list a null b | keep { not { is-null } } | join ,
a,b
cmdr> text $(get $config proxy null)
cmdr: text: null is not text
```

## Dicts

A dict maps text keys to values, in insertion order, as Python 3.7 and later. Values are constant, so commands that change a dict return a new one.

| cmdr | Python | Result |
|-|-|-|
| `dict [KEY VALUE ...]` | `dict(...)` | A dict; a repeated key keeps the last value |
| `get DICT KEY [DEFAULT]` | `d[k]`, `d.get(k, default)` | The value; a missing key without DEFAULT is an error |
| `has DICT KEY` | `k in d` | DICT if KEY is present; otherwise a mismatch |
| `set DICT KEY VALUE` | `d \| {k: v}` | A copy with KEY bound to VALUE |
| `unset DICT KEY` | `del d[k]` on a copy | A copy without KEY; a missing key is an error |
| `merge DICT [DICT ...]` | `a \| b` | Later dicts win on shared keys |
| `keys DICT`, `values DICT` | `list(d)`, `list(d.values())` | Lists in insertion order |
| `items DICT` | `list(d.items())` | A list of two-item lists |

`count` gives the number of keys, and `each` iterates over keys, as Python's `len(d)` and `for k in d`. A number key acts as its spelling, so `get $d 1` reads the key `"1"`.

```text
cmdr> dict name cmdr stars 3 | set stars $(add 3 1) | json.dumps
{"name": "cmdr", "stars": 4}
cmdr> read package.json | json.loads | get scripts | keys | join ,
build,test
```

## JSON

A `json` module mirrors Python's `json.loads` and `json.dumps`.

| JSON | cmdr |
|-|-|
| object | dict |
| array | list |
| string | text |
| number | number, keeping its spelling |
| `true`, `false` | bool |
| `null` | null |

`json.loads TEXT`:

- A duplicate key keeps the last value, as Python does.

- A number keeps its spelling, so an integer id above 2^53 keeps its digits until arithmetic touches it. Python makes it an exact `int`; a double cannot.

- `NaN` and `Infinity` are errors, since numbers are finite. Python accepts them.

- `\u0000` in a string is an error, since text cannot hold NUL.

`json.dumps VALUE [INDENT]`:

- Separators and `ensure_ascii` follow Python's defaults: `", "`, `": "`, and non-ASCII escaped as `\uXXXX`.

- A number writes its spelling if that is valid JSON, and its shortest form otherwise. `1.` and `.5` are Python literals but not JSON.

- A block is an error, as Python's `TypeError`.

- Text that is not valid UTF-8 is an error. cmdr text is bytes; JSON strings are Unicode.

`read` never parses JSON. Text becomes structure only through `json.loads`.

## Printing and text commands

A number or bool given to a command that takes text acts as its spelling. This covers `upper`, `join`, `concat`, `format`, `write`, `run` arguments, and the REPL's output.

```text
cmdr> run head -n 3 notes.txt
cmdr> count $(list a b) | concat " items"
2 items
```

A computed number prints with the fewest digits that read back exactly, as Python's `repr(float)`. A whole number prints without `.0`: `div 4 2` gives `2`.

Text commands reject null, dicts, and lists that contain them. The REPL prints a dict, or a list that holds a dict or null, as `json.dumps` output. Other lists print as now, one item per line.

## Changes to existing commands

| Command | Now | Sketch |
|-|-|-|
| `count`, `length` | text | number |
| `run-with` STATUS | text | number, or `timeout` as text |
| `sort` | byte order | by kind, optional key |
| `lt`, `gt`, `le`, `ge` | parse text | also take numbers; reject bools |

## Departures from Python

| Python | cmdr | Reason |
|-|-|-|
| `int` and `float` | one number kind, a double | The implementation uses doubles. Integers are exact to 2^53. |
| `bool` is an `int`; `True + 1 == 2` | bool is not a number | Arithmetic on a bool is a mistake in glue code. |
| `"3" + 1` is a `TypeError` | arithmetic parses numeric text | `read` and `run` return text. Requiring `number` before every arithmetic stage repeats the conversion. |
| `"a" + str(3)`; `",".join([1])` is a `TypeError` | text commands accept numbers and bools | `run head -n 3` must work without `text 3`. |
| `str(1.10)` is `'1.1'` | a literal keeps its spelling | Version strings and padded values pass through. |
| `==` across types | `equals` on spellings, `eq` on numbers | One comparison would need a rule for `"3"` and `3`. Python's rule fails silently on text from files. |
| truthiness | none; predicates mismatch | Mismatch already carries truth, and composes mid-pipeline. |
| `True`, `False`, `None` | `true`, `false`, `null` | cmdr words are lowercase. JSON uses the same spelling. |
| `str(None)` is `'None'` | text commands reject null | A null written into a file name or argument is a silent bug. |
| any hashable dict key | text keys; a number key acts as its spelling | JSON keys are strings. Python's `json.dumps` converts int keys the same way. |
| dicts are mutable | `set`, `unset`, and `merge` return new dicts | cmdr values are constant, and `as` bindings cannot change. |
| `d[k]` and `l[i]` share syntax | `get` for dicts, `nth` for lists from 1 | `nth` exists and counts from 1. |
| `json.loads` gives `1.10` as the float `1.1` | keeps the spelling | Ids and version-like numbers pass through unchanged. |
| `json.loads` accepts `NaN` and `Infinity` | error | Numbers are finite. |

## Extensions

The `cmdr_value` struct changes, so this needs an ABI version. To limit breakage:

- Numbers and bools always fill `.text` with their spelling. Code that reads only `.text` keeps working.

- Commands with `.text = 1` receive numbers and bools as text. Nine extension commands use the flag.

- Null has no fields. A dict stores alternating keys and values in `items`, which keeps insertion order. Extensions use accessors such as `cmdr_dict_get`, so a hash index can be added later without another ABI change.

- Explicit checks such as `argv[0].kind != CMDR_TEXT` (`ext/random/random.c:160`) reject the new kinds. They move to an accessor that accepts any scalar before the kinds land.

## Costs

Beyond the implementation work, the main cost is that a value's behavior depends on where it came from, and the REPL does not show it. Today `3` acts the same from a literal, `read`, `run`, or `add`. Under this sketch, values that print alike act differently:

| Pipeline | Result |
|-|-|
| `list 10 9 2 \| sort` | `2 9 10` |
| `read n.txt \| lines \| sort`, same digits | `10 2 9` |
| `list 3 \| json.dumps` | `[3]` |
| `read n.txt \| lines \| json.dumps` | `["3"]` |
| `list 1 null` | prints as JSON |
| `list 1 "null"` | prints one item per line |

Typed shells have this failure mode. PowerShell converts the right operand to the left operand's type, so a comparison can change when its operands swap ([about_Comparison_Operators](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_comparison_operators)).

Three choices in this sketch make it worse:

- Lexical literals let the source decide the kind. Quoting `3` changes `sort` and `json.dumps`.
- Implicit widening hides the kind at text commands. The difference shows up later, in `sort`, `eq`, or `json.dumps`.
- Mismatch and bool both express truth. `test` and `is` exist only to convert between them.

### Mitigations

| Mitigation | Removes | Costs |
|-|-|-|
| The REPL shows kind, printing text `3` as `"3"` | invisible kinds at the REPL | REPL output is no longer the data, so it stops working as glue |
| Literals stay text; numbers come only from arithmetic, `count`, and `json.loads` | most provenance differences | `list 3 \| json.dumps` gives `["3"]`, against Python |
| `sort` keeps byte order; typed order only through `sort { number }` | the most common silent difference | `sort` departs from Python's `sorted` |

### Alternative: JSON values as opaque handles

cmdr stays text-first. A `json` module returns handles that only `json.*` commands read; core commands never see a dict, null, or bool. Provenance differences then appear only where a script asked for JSON. The cost is two value systems, with explicit conversion at each crossing.

The choice between this sketch and the alternative is whether cmdr becomes a general typed language or stays text-first glue that handles JSON. This sketch assumes the first.

## Open questions

1. Bool operations: `and`, `or`, and `not` names are taken or mean something else. Candidates are `bool.and`, `bool.or`, and `bool.not` in a module.

2. Should `if` take a bool value directly, or only through `is`?

3. Iterating `items` passes each pair as one list, so `{ (k v) ... }` cannot take it. Candidates are an `each-item DICT BLOCK` that passes KEY and VALUE, or list destructuring in parameters, as Python's `for k, v in d.items()`.

4. Does `as` with a number literal keep its spelling? `as 1.10 v` then `$v` prints `1.10` under this sketch.

5. General typed language, or text-first glue with opaque JSON handles? See [Costs](#costs).
