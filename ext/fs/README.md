# fs

Test, inspect, create, move, and delete paths.

| Command | Result |
|-|-|
| `fs-exists PATH` | PATH if it exists, following links. A mismatch if not. |
| `fs-is-file PATH` | PATH if it is a regular file. A mismatch if not. |
| `fs-is-dir PATH` | PATH if it is a directory. A mismatch if not. |
| `fs-dirs [DIRECTORY]` | The subdirectories of DIRECTORY (default `.`), sorted by path, as `files` lists files. |
| `fs-size PATH` | The size in bytes. |
| `fs-mtime PATH` | The modification time, in seconds since the epoch. |
| `fs-mkdir PATH` | Create PATH and missing parents, as `mkdir -p` does, and return PATH. |
| `fs-remove PATH` | Delete a file, link, or empty directory, and return PATH. |
| `fs-move SOURCE DESTINATION` | Rename SOURCE, and return DESTINATION. |

```text
cmdr> files . | keep { (f) fs-size $f | gt 1000 } | count
5
cmdr> list a b | prefix build/ | each { fs-mkdir }
build/a
build/b
```

The predicates fail with an error, not a mismatch, when PATH cannot be checked, such as when a parent directory denies search. So `not { fs-exists }` holds only when PATH is absent.

`fs-remove` and `fs-move` change files without asking. `fs-move` replaces an existing file at DESTINATION, and fails across file systems. `fs-remove` does not recurse, so a nonempty directory is an error.

Without `stat`, `try { read }` cannot tell a missing file from an unreadable one, and `sh 'test -e ...'` runs a process per path and needs shell quoting.
