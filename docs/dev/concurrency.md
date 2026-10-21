# Concurrency model

Description, 2026-10-21, of `f2738e5` plus the working tree, including `run-all`. For ways to extend it, see [async.md](async.md).

## Summary

cmdr evaluates sequentially, one thread per session. The language has no concurrency of its own. A host gets parallelism by running separate sessions on separate threads. A script gets it by running programs in parallel with `run-all`.

## In the language

- Pipelines are sequential. `a | b` finishes `a` and holds its whole value before it calls `b` (`README.md:47`). Stages are not processes and do not stream.
- Sub-pipelines `$(...)` run left to right, before their stage's call (`src/cmdr.c:759`).
- `run`, `run-in`, and `run-with` start one program with `posix_spawn` and wait for it to exit. Its stdin and stdout, and `run-with`'s captured stderr, are temporary files, not pipes (`process_stream` and `spawn_process` in `src/primitives.c`). A program that writes before it reads cannot block, and cmdr sees no output until the program exits.
- `run-all JOBS N` runs up to N programs at once and waits for all of them. It returns each job's `STATUS STDOUT STDERR` in input order, so results do not depend on which finishes first.
- There is no background job, parallel map over blocks, or `await`. cmdr code never runs while a program runs.

## In a host

- A session is used by one thread at a time. Sessions on different threads are independent (`include/cmdr.h:148-149`).
- Call and `$(...)` depth are counted per thread, in a `_Thread_local` (`src/cmdr.c:17`). The limit is 128.
- The out-of-memory handler must be set before any thread evaluates (`include/cmdr.h:65-69`).
- A block value cannot move between threads. Its compiled code is shared by copies with a plain `size_t` reference count (`src/cmdr.c:205`), and its scope points into the session that wrote it.

## Process-wide state

| State | Where | Effect |
|-|-|-|
| Interrupt flag | `interrupted` in `src/primitives.c` | `cmdr_interrupt(1)` stops every session on every thread until the host clears it. |
| Wake pipe | `wake_read`, `wake_write` in `src/primitives.c` | Made once, on the first wait. A set flag wakes every thread's wait in `run`. |
| SIGCHLD | `include/cmdr.h:75-76` | The host must not ignore SIGCHLD or reap `run`'s children. |
| `random` generator | `ext/random/README.md:29` | One per thread. Sessions on a thread share it. |
| `snd` lock | `examples/sox-dsl/ext/snd/README.md:38` | libsox keeps global state, so `snd` commands run one at a time. |
| fd 2 during `snd.report` | same | Every thread's stderr goes into the result while it runs. |
| umask | `snd.c`, `midi.c` | Set to 077 for one moment, on the first output file. |

`env` only reads the environment. No command calls `chdir`: `run-with dir` changes directory in the child.

## Interrupts

1. A SIGINT or SIGTERM handler calls `cmdr_interrupt(1)`. `src/main.c` installs one for each signal that was not ignored, and exits by the first one received.
2. Each stage checks the flag before it runs, so a block of value stages also stops (`src/cmdr.c:737`).
3. A wait in `run` or `run-all` blocks on the children's exits and the wake pipe: a pidfd and `poll` on Linux, kqueue on BSD and macOS, a 10 ms poll elsewhere (`block` and `wait_until` in `src/primitives.c`).
4. Each running program gets SIGTERM, then SIGKILL a second later, and is reaped. `run-all` starts no more jobs. With `group 1`, the whole process group is signalled.
5. The stage fails with `interrupted`, which `try` does not catch.

## Consequences

- A script can overlap programs only through `run-all`, which waits for all of them. It cannot run cmdr code while they run.
- Scripts cannot act on output while a program runs, as `tail -f | filter` would.
- Results do not depend on timing. The only nondeterminism is in the programs a script runs.
