# json extension

Design sketch, 2026-10-21. Status: nothing implemented. Examples show intended output.

## Problem

JSON goes through `jq` with `run`. `llama-chat` (`examples/agent-workflows/aw/__init__.cmdr:37`) runs `jq` twice: once to build a request body, once to read `.choices[0].message.content`. A script without `jq` installed cannot read an API response.

[TODO.md](../../TODO.md) names two options: a map value kind, which [rich-types.md](rich-types.md) sketches, or an extension that converts JSON to lists of key and value pairs. Pairs lose JSON's distinctions: `list a b` could be the array `["a", "b"]` or the object `{"a": "b"}`, and `3` could be a number or a string.

This sketch takes a third option. JSON stays text, and `json.*` commands read and build it by path. It needs no change to `cmdr_value`, and it works under either outcome of rich-types.md.

## Model

- A JSON value is text holding valid JSON.
- A path is the arguments after the JSON: each is a key into an object, or an index into an array.
- Commands that return JSON return the source slice unchanged. Numbers keep their spelling, keys keep their order, and a 64-bit id keeps its digits.
- Only `json.text` and `json.keys` decode JSON strings into text.

Path segments are separate arguments, not a `.a.b` path string, so a key may contain `.`, `[`, or spaces without escaping.

## Commands

### Reading

| Command | Result |
|-|-|
| `json.get JSON [SEG ...]` | The value at the path, as JSON |
| `json.text JSON [SEG ...]` | The string at the path, decoded; error if it is not a string |
| `json.type JSON [SEG ...]` | `object`, `array`, `string`, `number`, `bool`, or `null` |
| `json.keys JSON [SEG ...]` | The object's keys, decoded, in order |
| `json.values JSON [SEG ...]` | The object's values or the array's items, each as JSON |
| `json.has JSON [SEG ...]` | JSON if the path exists; otherwise a mismatch |
| `json.valid TEXT` | TEXT if it is one valid JSON value; otherwise a mismatch |

A segment is a key when the value is an object. When it is an array, a segment is an index from 0, and a negative index counts from the end, as in Python and jq. A path follows JSON's convention, and `nth` follows cmdr's, so a path copied from jq or API docs selects the same item.

A missing key or an index out of range is a mismatch, so `or` supplies a default. A segment applied to a string, number, bool, or null is an error, as is malformed JSON.

```text
cmdr> import json
cmdr> read reply.json | json.text choices 0 message content
Hello.
cmdr> read reply.json | json.get usage
{"prompt_tokens": 12, "completion_tokens": 3}
cmdr> read reply.json | json.get usage completion_tokens | add 1
4
cmdr> or { () read reply.json | json.text model } unknown
unknown
```

A number comes back as its spelling, which arithmetic already parses. `true`, `false`, and `null` come back as those words; `json.type` tells `null` from the string `"null"`, which comes back quoted.

### Building

| Command | Result |
|-|-|
| `json.str TEXT` | TEXT as a JSON string |
| `json.array [JSON ...]` | An array of the arguments; a list argument adds its items |
| `json.object [KEY JSON ...]` | An object; each KEY is text, each value JSON |
| `json.strings [KEY TEXT ...]` | An object whose values are strings |
| `json.set JSON SEG ... VALUE` | JSON with VALUE, itself JSON, at the path; the last key may be new |
| `json.format JSON [INDENT]` | JSON reformatted, as Python's `json.dumps(json.loads(...))` |

Values given to `json.array`, `json.object`, and `json.set` must be valid JSON, so `json.object n 3` makes `{"n": 3}` and `json.object n x` is an error. Text becomes a string only through `json.str` or `json.strings`. The extension never guesses whether `3` is a number or a string.

`json.set` splices VALUE into the source, so the rest keeps its spelling and layout.

### `llama-chat` without `jq`

```text
def llama-body { (q m) json.strings role user content $q | json.array | with { (ms) json.object messages $ms } | with { (b) or { () nonempty $m | json.str | with { (s) json.set $b model $s } } $b } }
```

The reply becomes `... | llama-run $body | json.text choices 0 message content`.

## Departures from Python's `json`

| Python | `json` extension | Reason |
|-|-|-|
| `loads` builds objects | values stay text; commands take paths | cmdr has no dict kind. See [rich-types.md](rich-types.md). |
| `d["k"]` raises `KeyError` | a missing key mismatches | `or` gives a default, as for any predicate. |
| `1.10` loads as `1.1` | `json.get` returns `1.10` | Source slices keep spelling. |
| `loads` accepts `NaN`, `Infinity` | error | RFC 8259 excludes them. |
| `loads` accepts a lone surrogate, `"\ud800"` | error in `json.text` | Text is bytes, and UTF-8 cannot encode a lone surrogate. |
| `"\u0000"` decodes | error in `json.text` | Text cannot hold NUL. |

Separators, `ensure_ascii`, and duplicate keys follow Python: `", "` and `": "`, non-ASCII escaped as `\uXXXX`, and the last duplicate wins.

## Implementation

- A hand-written scanner over spans, in `ext/json/json.c`. It validates RFC 8259 and returns the byte range of the value at a path, without building a tree.
- Nesting deeper than a fixed limit, such as 512, is an error, so hostile input cannot overflow the C stack.
- Invalid UTF-8 in the input is an error, as RFC 8259 requires UTF-8.
- `json.format` and the builders write output; the readers only slice.

A hand-written scanner over a library, because spans and spellings are the point. cJSON stores numbers as doubles and loses their spelling. jsmn returns spans but is permissive unless built with `JSMN_STRICT`. These are from the libraries' documentation, not tested here.

Each command scans its input again, so a loop of `json.get` calls over a large array costs O(n^2). `json.values` once, then `json.get` per item, is O(n).

Tests: a `tests/test.cmdr` beside the C unit tests, as for `shlex`, plus the y_ and n_ cases of [JSONTestSuite](https://github.com/nst/JSONTestSuite) for `json.valid`.

## Relation to rich-types.md

- If cmdr gains dicts, `json.loads` and `json.dumps` join this module. The path commands stay: they read a large response without converting it.
- If cmdr stays text-first, this module is the alternative that sketch describes. JSON text takes the place of an opaque handle, at the cost of a scan per command.

## Open questions

1. Should `json.get` on a string return it decoded, as `jq -r` does? That saves `json.text`, but `null` and `"null"` then print alike.
2. Is `json.set` enough, or do scripts need `json.unset` and a merge?
3. Should builders emit compact JSON, `{"a":1}`, for request bodies? Python's default separators add spaces.
