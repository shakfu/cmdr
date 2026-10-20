# env

Read environment variables.

| Command | Result |
|-|-|
| `env.get NAME` | The value of `NAME`. A mismatch if `NAME` is unset, so `or` and `not` can test it. |
| `env.or NAME FALLBACK` | The value of `NAME`, or `FALLBACK` if unset. |
| `env.path-dirs` | The directories in `PATH`, as a list. |

```text
cmdr> import env
cmdr> env.or EDITOR vi
vi
cmdr> env.path-dirs | count
7
```

`env` is in C because cmdr has no primitive that reads the environment. `sh 'echo $NAME'` can, but it starts a process per call and cannot tell an unset variable from an empty one.
