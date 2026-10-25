# proc

Check and signal processes by id, such as the ones `spawn` starts.

| Command | Result |
|-|-|
| `proc.alive PID` | PID if its process is running. A mismatch if there is none, or it has exited and awaits reaping. |
| `proc.kill PID [SIGNAL]` | Send SIGNAL, default `TERM`, and return PID. A negative PID names a process group, as in kill(2). A mismatch if there is no such process. |

```text
cmdr> import proc
cmdr> spawn $(list) sleep 60 | as job
cmdr> proc.alive $job
4242
cmdr> proc.kill $job
4242
cmdr> retry 20 every 0.25 { () not $job { proc.alive } }
4242
```

SIGNAL is a name, with or without `SIG`, such as `TERM`, `SIGKILL`, `HUP`, `INT`, or `USR1`, or a number. `proc.kill` refuses PID 0 and -1, which signal cmdr's own group and every process the user owns.

`proc.alive` holds for a process cmdr may not signal, since one that exists is running. A zombie, a process that has exited before its parent reaped it, is not alive. On Linux and macOS, `proc.alive` reads the process state to tell; elsewhere a zombie counts as alive. `proc.kill` succeeds on a zombie, as kill(2) does.

A pid is reused once its process is reaped, so `proc.alive` on an old pid may find a new process. `spawn` reaps its programs only at the next `spawn`, so their pids stay theirs until then.

`run kill` and `run ps` start a process per call. `ps -o stat=` differs between procps and BSD, and `kill` reports a missing process only by its exit status.
