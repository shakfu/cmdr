# fs

Test, inspect, list, create, copy, move, and delete paths.

| Command | Result |
|-|-|
| `fs.exists PATH` | PATH if it exists, following links. A mismatch if not. |
| `fs.is-file PATH` | PATH if it is a regular file. A mismatch if not. |
| `fs.is-dir PATH` | PATH if it is a directory. A mismatch if not. |
| `fs.is-link PATH` | PATH if it is a symbolic link, dangling or not. A mismatch if not. |
| `fs.dirs [DIRECTORY]` | The subdirectories of DIRECTORY (default `.`), sorted by path, as `files` lists files. Links to directories are not listed, as with `find -type d`. |
| `fs.walk [DIRECTORY]` | Every path below DIRECTORY (default `.`), recursively, sorted by path. Links are listed, not followed. |
| `fs.size PATH` | The size in bytes. |
| `fs.mtime PATH` | The modification time, in seconds since the epoch. |
| `fs.realpath PATH` | The absolute path, with links, `.`, and `..` resolved. PATH must exist. |
| `fs.mkdir PATH` | Create PATH and missing parents, as `mkdir -p` does, and return PATH. |
| `fs.temp-file [TEMPLATE]`, `fs.temp-dir [TEMPLATE]` | Create a new empty file, mode 0600, or directory, mode 0700, and return its path. TEMPLATE ends in `XXXXXX`, as for `mktemp`; the default is `cmdr-XXXXXX` in `TMPDIR`, or `/tmp`. |
| `fs.symlink TARGET PATH` | Create a link at PATH to TARGET, as `ln -s` does, and return PATH. |
| `fs.copy SOURCE DESTINATION` | Copy a file, link, or directory tree to DESTINATION, which must not exist, and return DESTINATION. |
| `fs.remove PATH` | Delete a file, link, or empty directory, and return PATH. |
| `fs.move SOURCE DESTINATION` | Rename SOURCE, and return DESTINATION. |
| `fs.remove-tree PATH` | Delete PATH and everything below it, as `rm -r` does, and return PATH. |

```text
cmdr> import fs
cmdr> files . | keep { (f) fs.size $f | gt 1000 } | count
5
cmdr> list a b | prefix build/ | each { fs.mkdir }
build/a
build/b
```

The predicates fail with an error, not a mismatch, when PATH cannot be checked, such as when a parent directory denies search. So `not { fs.exists }` holds only when PATH is absent.

`fs.remove`, `fs.remove-tree`, and `fs.move` change files without asking. `fs.move` replaces an existing file at DESTINATION, and fails across file systems. `fs.remove` does not recurse, so a nonempty directory is an error.

`fs.remove-tree` never follows a link: a link is removed, not what it points to, even written as `link/`. It opens each directory without following links, so a directory swapped for a link mid-removal is not entered. It refuses `/` and a path ending in `.` or `..`. A failure stops it partway, with the failing path in the message. `rm -rf` ignores a missing PATH; `fs.remove-tree` fails, as Python's `shutil.rmtree` does, so test with `fs.exists` first.

`fs.copy` follows Python's `shutil.copytree(src, dst, symlinks=True)`: links are copied as links, and an existing DESTINATION is an error, where `cp -R` would copy into it. Permission bits are copied, less the umask; owners and times are not. A directory cannot be copied into itself. Devices, FIFOs, and sockets are errors. A failure leaves a partial copy.

`fs.walk` skips entries removed during the walk, as `fs.dirs` does. With `fs.is-file`, `fs.is-dir`, and `fs.is-link`, it replaces `find`. `fs.is-file` follows links, so `find src -type f` is `fs.walk src | keep { fs.is-file } | keep { not { fs.is-link } }`.

Without `stat`, `try { read }` cannot tell a missing file from an unreadable one, and `sh 'test -e ...'` runs a process per path and needs shell quoting.
