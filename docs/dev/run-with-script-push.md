# run-with, script commands, and push

Decision record, 2026-10-19, against `38ed7c7` plus the working tree. [examples/agent](../../examples/agent/README.md) exposed five gaps:

1. `run` has no working directory.

2. `run` drops stderr and the exit status.

3. `run` has no timeout.

4. A script cannot find its own file or directory.

5. No command appends to a list. `list $xs x` nests `$xs` as one item.

6. A script cannot take arguments: `cmdr FILE a b` was a usage error.

Each fix was checked with `make test` (60 tests) and with the Python suite against an ASan and UBSan build. The Linux build has not been compiled; see [Working directory](#working-directory).

## run-with

### Decision

- `run-with OPTIONS PROGRAM [ARG ...]`, an effect primitive.

- OPTIONS is a list of NAME VALUE pairs: `dir`, `stdin`, `timeout`. An unknown or repeated name is an error.

- The result is the list `STATUS STDOUT STDERR`. A nonzero status is a result, not a failure.

- STATUS is the exit status, 128 plus the signal number, or `timeout`.

- A list argument adds its items as arguments, at any depth. An empty list adds none.

- `run` and `run-in` are unchanged. All three share one `execute`.

### Alternatives

| Choice | Over | Because |
|-|-|-|
| One command with options | `run-at`, `run-err`, `run-timeout`, ... | 3 options combine into 8 variants |
| A new command | New options on `run` | `run` takes only text and returns stdout; a list result would break every caller |
| Status as data | Fail on nonzero, with stderr in the message | `grep` without matches exits 1, and a caller may branch on it. Failing on nonzero is one `def` (`agent-out` in the example) |
| NAME VALUE pairs | `name=value` items | A computed value needs no `concat`: `list dir $d timeout $t` |
| `timeout` as STATUS | 124, as GNU `timeout` returns | A program can exit 124 itself |
| Flatten at any depth | One level | `list claude -p $model_flag $p` holds `$model_flag` as a nested list |

### Working directory

| Mechanism | Problem |
|-|-|
| `posix_spawn_file_actions_addchdir` (POSIX.1-2024) | New: macOS 26 declares it; older platforms lack it |
| `posix_spawn_file_actions_addchdir_np` | Not POSIX: glibc 2.29, macOS 10.15 to 26 (deprecated in the macOS 26 SDK) |
| `fork`, `chdir`, `execvp` | `execvp` is not async-signal-safe, so a child forked from a threaded host can deadlock |
| `sh -c 'cd "$1" && exec "$@"'` inside C | Moves the example's workaround into C; a missing program becomes exit status 127, not a spawn error |
| `chdir` in the parent around `posix_spawnp` | The working directory belongs to the process, so it races with other sessions' threads |

CMake checks for the POSIX name, then the `_np` name, under the feature macros of `src/primitives.c`. With neither, `dir` is an error. Only the POSIX path was compiled, on macOS 26.7.1. The glibc `_np` path needs `_DEFAULT_SOURCE` and has not been built.

A missing or non-directory `dir` is checked with `stat` before spawning. Otherwise a failed file action and a missing program both report `ENOENT`.

### Timeout

- `waitpid` with `WNOHANG` polls every 10 ms. A `SIGCHLD` handler or `sigtimedwait` would change signal state that belongs to the host.

- At the limit the child gets SIGTERM, then SIGKILL after 1 s.

- The child stays in cmdr's process group, so Ctrl-C at the terminal still reaches it. With its own group, a timeout could kill the whole tree, but Ctrl-C would leave an agent running and editing files. The cost: the child's own children are not signalled.

## script-file and script-dir

### Decision

- `script-file` and `script-dir` are session built-ins. They return the absolute path, and the directory, of the script file in which the call is written, like `realpath(__file__)` in Python.

- Resolution follows definitions. `cmdr_session_load` pushes layers whose commands return the script's path. When the script ends, it pushes layers restoring the outer path. Lines compiled meanwhile, including `def` bodies, resolve to those layers.

- Outside a script file, both fail with `not in a script file`. This covers the REPL, `-c`, embedded sources (the prelude, extension scripts), and a path that `realpath` cannot resolve.

### Alternatives

| Choice | Over | Because |
|-|-|-|
| The file where the call is written | The running script | A `def` called after its script ends would see the caller's script. `def` rejects blocks with captured values, so a script cannot store the value at load time |
| Commands | `load` resolving relative paths against the script | Breaks scripts that load paths relative to the working directory, as the example's first `demo.cmdr` did |
| Absolute | The path as given to `load` | It stays valid in `run-with` children with another `dir` |
| `script-file` and `script-dir` | `here` | `here` named neither a file nor a directory. The prefix pairs them, as `fs-` and `run-` do |
| Both | Only `script-file`, with `path-dirname` | Most callers want the directory; `script-dir` saves a stage in each |

Each `load` adds two layers, one on entry and one on exit, each holding the three script commands. They are freed at `cmdr_session_close`, as definitions are. `help` lists each command once, at the built-ins.

## script-args

### Decision

- `cmdr FILE [ARG ...]` and `load PATH [ARG ...]` pass ARGs to the script. `script-args` returns them as a list, without the path, which `script-file` gives.

- It resolves as `script-file` does: to the script in which the call is written. A `def` keeps its script's arguments after the script ends.

- `cmdr -c PIPELINE` takes no arguments. Nothing needs them yet.

- `cmdr_session_load_args` passes arguments from a host. `cmdr_session_load` passes none, so its callers are unchanged.

### Alternatives

| Choice | Over | Because |
|-|-|-|
| Per script | One process-wide list, like Python's `sys.argv` | A loaded library could come to depend on its caller's command line. `load` with arguments lets one script parameterize another without environment variables |
| A list of text | Parsed flags | Flag parsing is the script's job; `starts-with` and `strip-prefix` cover it |
| A new API function | Arguments added to `cmdr_session_load` | Existing hosts keep compiling |

## push

`push LIST_OR_LINES [ITEM ...]`, a pure primitive. Items are cloned, and a list item stays one item.

| Choice | Over | Because |
|-|-|-|
| `push` | `extend LIST LIST` | `fold $ys $xs { (a x) push $a $x }` gives `extend`; `extend` cannot add a single list as one item |
| `push` | `flatten` | Flattening cannot keep a list as one item |
| The name `push` | `append` | `append` writes to a file |

## Remaining

- `nth` cannot return a list item: `last` ends in `join ""`. The example keeps its history as a flat list of headings and outputs for this reason.

- Each computed argument needs one `with` level. The example's adapters are three or four levels deep.
