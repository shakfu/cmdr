# hashlib

SHA-256 digests in hex, as Python's `hashlib.sha256(data).hexdigest()`.

| Command | Result |
|-|-|
| `hashlib.sha256 TEXT` | The SHA-256 digest of TEXT's bytes, as 64 lowercase hex digits. |
| `hashlib.sha256-file PATH` | The SHA-256 digest of the file at PATH, read in chunks. The file may hold any bytes. |

```text
cmdr> import hashlib
cmdr> hashlib.sha256 abc
ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
cmdr> hashlib.sha256-file release.tar.gz | equals $(read release.tar.gz.sha256 | words | first)
```

`hashlib.sha256 $(read PATH)` and `hashlib.sha256-file PATH` agree on text files. Use the file form for binary files, which `read` rejects, and for large ones, which it holds in memory. The implementation follows FIPS 180-4, so cmdr links no crypto library.
