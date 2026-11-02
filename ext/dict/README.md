# dict

Dicts: text keys mapped to values, in insertion order, as Python 3.7 dicts. A dict comes only from these commands and `json.to-dict`; no literal and no other command makes one. Commands return a new dict; values are constant.

| Command | Result | Python |
|-|-|-|
| `dict.of [KEY VALUE ...]` | A dict. A repeated KEY keeps its first place and its last VALUE. | `dict(...)` |
| `dict.from-items LIST` | A dict of a list of KEY VALUE pairs. | `dict(pairs)` |
| `dict.get DICT KEY` | The value under KEY. A mismatch if there is none. | `d[k]` |
| `dict.has DICT KEY` | DICT if it has KEY. A mismatch if not. | `k in d` |
| `dict.set DICT KEY VALUE [KEY VALUE ...]` | DICT with each KEY set to VALUE. | `d \| {k: v}` |
| `dict.unset DICT [KEY ...]` | DICT without the KEYs. A missing KEY is ignored. | `{k: v for ... if k not in ks}` |
| `dict.merge DICT [DICT ...]` | The entries of each DICT; later ones win. | `a \| b` |
| `dict.keys DICT`, `dict.values DICT` | Lists, in order. | `list(d)`, `list(d.values())` |
| `dict.items DICT` | A list of KEY VALUE pairs. | `list(d.items())` |
| `dict.count DICT` | The number of entries. | `len(d)` |

```text
cmdr> import dict
cmdr> dict.of name cmdr stars 3 | dict.set stars 4
name	cmdr
stars	4
cmdr> or { () dict.of a 1 | dict.get b } none
none
cmdr> text "a=1\nb=2" | lines | map split = | dict.from-items | dict.get b
2
```

A missing key mismatches, as `json.get` does, so `or` supplies a default. Python raises `KeyError`.

A dict prints one entry per line: KEY, a tab, VALUE. So `write`, the REPL, and pipes give text that `lines` and `split` read back. A dict whose values are not all text, or whose keys or values hold a tab or newline, cannot print: convert it first, for example with `json.from-dict`.

Commands outside this module reject a dict: `count $d` fails with `expected text, got a dict`. `nonempty` mismatches on an empty dict. Values may be any value, including lists and dicts.

Lookup is linear in the entries. A dict suits records and options, not tables of thousands of keys.
