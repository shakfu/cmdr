# base64

Encode and decode base64, as Python's `base64.b64encode` and `b64decode`, with the standard alphabet and `=` padding.

| Command | Result |
|-|-|
| `base64.encode TEXT` | TEXT's bytes as base64, on one line. |
| `base64.encode-file PATH` | The bytes of the file at PATH as base64, on one line. The file may hold any bytes. |
| `base64.decode TEXT` | The bytes TEXT encodes. Spaces, tabs, and line breaks are skipped. Another byte outside the alphabet, or wrong padding, is an error. |

```text
cmdr> import base64
cmdr> base64.encode 'hello, world'
aGVsbG8sIHdvcmxk
cmdr> base64.decode aGVsbG8sIHdvcmxk
hello, world
```

Text cannot hold a NUL byte, so `base64.decode` fails on data that holds one, and `base64.encode-file` is the way to encode binary data. Python's `b64decode` skips bytes outside the alphabet unless `validate=True`; this module rejects them, except whitespace, so a corrupted input fails.
