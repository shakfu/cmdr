# lists

List operations that need a position or a second list, as Python's `itertools` and list methods give them. Each takes a list or newline-separated text, as list primitives do, and returns a list.

| Command | Result | Python |
|-|-|-|
| `lists.enumerate XS [START]` | A pair per item: its position, from START (default 1), then the item. | `enumerate(xs, 1)` |
| `lists.zip XS YS ...` | A list per position, holding each argument's item there; as long as the shortest. | `zip(xs, ys)` |
| `lists.chain [XS ...]` | The items of each argument in turn. | `itertools.chain` |
| `lists.flatten XS` | The items of each list item, and each text item itself. | `chain.from_iterable` |
| `lists.set-at XS I VALUE` | XS with item I, from 1, replaced by VALUE. An error if I is out of range, so `or` cannot hide a bad write. | `xs[i - 1] = v` |
| `lists.index-of XS TEXT` | The position, from 1, of the first item equal to TEXT. A mismatch if there is none. | `xs.index(t) + 1` |
| `lists.sort-by XS BLOCK [ARG ...]` | The items, sorted by the text BLOCK returns for each, in byte order. | `sorted(xs, key=f)` |
| `lists.sort-n-by XS BLOCK [ARG ...]` | The same, with numeric keys, as `sort-n` sorts. | |
| `lists.group-by XS BLOCK [ARG ...]` | A pair per run of adjacent items with equal keys: the key, then a list of the items. | `itertools.groupby` |
| `lists.batched XS N` | Lists of N items; the last may be shorter. | `itertools.batched` |
| `lists.accumulate XS BLOCK [ARG ...]` | The first item, then BLOCK's result on the previous result and each next item. | `itertools.accumulate` |

```text
cmdr> import lists
cmdr> list a b c | lists.enumerate | each { (p) unwords $p }
1 a
2 b
3 c
cmdr> files . | lists.sort-n-by { fs.size } | tail 1
cmdr> text a/b/c | split / | lists.accumulate { (p c) path-join $p $c }
a
a/b
a/b/c
```

Positions count from 1, as `nth` counts. The sorts are stable, so items with equal keys keep their order. `lists.group-by` groups only adjacent items, as `itertools.groupby` does; sort by the same key first to group them all.

A block's arguments are the item, then ARGs, as for `each`; `lists.accumulate`'s are the previous result, the item, then ARGs, as for `fold`. A block that fails, or mismatches, fails the command with the item's position.

`nth` walks the list, so `count $xs | num.range | each { (i) nth $xs $i }` is quadratic; `lists.enumerate` and `lists.zip` are linear. Splicing an item, chaining lists, and keeping each partial `fold` result otherwise need a `fold` that copies its accumulator per item.
