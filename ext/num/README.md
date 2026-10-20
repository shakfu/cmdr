# num

Number sequences, modulo, and rounding. Results print as `add` and `div` results do.

| Command | Result |
|-|-|
| `num.seq FIRST LAST [STEP]` | A list from FIRST to LAST inclusive, by STEP (default 1). Empty if STEP points away from LAST. More than 10,000,000 items is an error. |
| `num.range N` | `num.seq 1 N`. |
| `num.mod A B` | A modulo B, with the sign of B: `num.mod -1 3` is `2`. |
| `num.floor NUMBER`, `num.ceil NUMBER` | NUMBER rounded down or up. |
| `num.round NUMBER` | NUMBER rounded to the nearest integer, halves away from zero. |
| `num.abs NUMBER` | The absolute value. |

```text
cmdr> import num
cmdr> num.range 3 | each { (i) text item $i }
item 1
item 2
item 3
```

A `def` cannot call itself, so a script cannot build a list of N items; `num.seq` gives `each` and `fold` something to loop over. `div` returns fractions, and rounding them by editing text fails on negatives and exponents.
