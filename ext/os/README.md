# os

The operating system's name, the host's name, the working directory, and this process's id.

| Command | Result | Python |
|-|-|-|
| `os.sysname` | The kernel's name, such as `Linux` or `Darwin`. | `os.uname().sysname` |
| `os.hostname` | The machine's network name. | `os.uname().nodename` |
| `os.cwd` | The current working directory, absolute. | `os.getcwd()` |
| `os.pid` | This process's id. | `os.getpid()` |

```text
cmdr> import os
cmdr> if { () os.sysname | equals Linux } { () text linux } else { () text other }
linux
cmdr> os.cwd | path-basename
cmdr
```

`run uname` starts a process per call; `sh 'pwd'` and `sh 'echo $PPID'` start a shell. `os.pid` names cmdr itself, for a lock or a file another process reads. `spawn` returns the ids of the programs cmdr starts; [proc](../proc/README.md) checks and signals them.
