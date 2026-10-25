# json

Read and build JSON kept as text. A path is the arguments after the JSON: each segment is a key into an object, or an index into an array. Readers return slices of their input, so numbers keep their spelling and keys their order.

| Command | Result |
|-|-|
| `json.get JSON [SEG ...]` | The value at the path, as JSON. |
| `json.text JSON [SEG ...]` | The string at the path, decoded. An error if it is not a string. |
| `json.type JSON [SEG ...]` | `object`, `array`, `string`, `number`, `bool`, or `null`. |
| `json.keys JSON [SEG ...]` | The object's keys, decoded, in order. |
| `json.values JSON [SEG ...]` | The object's values, or the array's items, each as JSON. |
| `json.has JSON [SEG ...]` | JSON if the path exists. A mismatch if not. |
| `json.valid TEXT` | TEXT if it is one valid JSON value. A mismatch if not. |
| `json.str TEXT` | TEXT as a JSON string. |
| `json.array [JSON ...]` | An array of the arguments. A list argument adds its items. |
| `json.object [KEY JSON ...]` | An object. Each KEY is text; each value must be JSON. |
| `json.strings [KEY TEXT ...]` | An object whose values are strings. |
| `json.set JSON SEG [SEG ...] VALUE` | JSON with VALUE, itself JSON, at the path. The last key may be new. |
| `json.format JSON [INDENT]` | JSON laid out as Python's `json.dumps(json.loads(JSON), indent=INDENT)`. |

```text
cmdr> import json
cmdr> read reply.json | json.text choices 0 message content
Hello.
cmdr> or { () read reply.json | json.text model } unknown
unknown
cmdr> json.strings role user content hi | json.array | with { (ms) json.object messages $ms }
{"messages": [{"role": "user", "content": "hi"}]}
```

An array index counts from 0, and a negative one from the end, as in Python and jq. So a path copied from jq or API docs selects the same item; `nth` counts from 1.

A missing key or an index out of range is a mismatch, so `or` supplies a default. Malformed JSON, nesting deeper than 512, and a segment applied to a string, number, bool, or null are errors.

Builders take JSON, never guess: `json.object n 3` is `{"n": 3}`, and `json.object n x` is an error. Text becomes a string only through `json.str` or `json.strings`. `json.set` splices VALUE into the source, so the rest keeps its layout.

Output follows Python's `json.dumps`: separators `", "` and `": "` (`","` with INDENT), and non-ASCII escaped as `\uXXXX`. Duplicate keys follow `json.loads`: the last value wins, at the first key's place. `json.format` departs from Python by keeping number spelling: `1.10` stays `1.10`.

JSON must be UTF-8, without `NaN` or `Infinity`, as RFC 8259 requires. `json.text` and `json.keys` fail on `\u0000` and lone surrogates, which text cannot hold; `json.format` keeps them escaped.

Each command scans its whole input. A loop of `json.get` over a large array is quadratic; take `json.values` once instead.

`jq` through `run` works where it is installed. The design, and the alternatives to JSON as text, are in [docs/dev/ext-json.md](../../docs/dev/ext-json.md).
