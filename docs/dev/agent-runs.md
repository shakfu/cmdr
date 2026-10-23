# Detached agent runs

Design, 2026-10-23. Status: B, with the module API, implemented.

## Problem

`agent.cmdr edit DIR claude PROMPT` blocks the shell until the agent exits. One review of `~/projects/hilda` took about 5 minutes, and printed nothing in that time. The need is twofold:

1. A run returns the prompt at once.

2. Several runs at once can be watched: step, agent, state, elapsed time.

3. A run's result can be used in a pipeline, in the REPL or a script, after other work.

Parallelism alone does not meet the need. `run-all` already runs N `agent.cmdr` processes at once, but the caller waits for all of them ([async.md](async.md#a-run-all)).

## Constraints in the current code

1. Every process primitive waits for its child. `run-with` takes `dir`, `stdin`, `timeout`, and `group` (`src/primitives.c:649`). It then waits in `wait_child` (`src/primitives.c:465`).

2. A child's output goes to a temporary file that is read after it exits ([concurrency.md](concurrency.md)). No one can follow a run while it is running.

3. `AW_RUNS` defaults to `agent-runs`, a path relative to the working directory (`examples/agent/agent/__init__.cmdr:11`). A `status` query from another directory would not find the run.

4. The steps of a chain cannot overlap, since step k reads step k-1's output (`examples/agent/agent/__init__.cmdr:108`).

5. A block cannot cross threads (`src/cmdr.c:205`). A redraw thread inside cmdr is not an option.

## Options

| | Option | Prompt back | C | Survives terminal close |
|-|-|-|-|-|
| A | `setsid agent.cmdr ... > log 2>&1 &` by hand | shell | none | yes |
| B | `agent.cmdr start` re-invokes itself detached; runs are directories | shell | one primitive | yes |
| C | Job handles in the session, `start` and `wait-job` ([async.md](async.md)) | cmdr REPL | job table, reaping | no, unless detached as well |
| D | A C `agent` extension with an in-process TUI | cmdr REPL | most of `agent` | no |

- **A** works today, but it has no run id, status, result lookup, or cancel.

- **C** gives runs as values in a pipeline. Its jobs live in one session's memory, so no other process sees them, and they end with the session.

- **D** moves orchestration into C, which changes often and needs no speed. A crash in the view also kills every run. It conflicts with linenoise over the terminal, and is hard to test under CTest.

Recommendation: B, with a module API that returns run ids as values. This meets need 3 without C's job table. The REPL and the shell then share one run model, so a run started in the REPL shows in `agent.cmdr status` elsewhere.

A handle value kind would let cmdr check a handle's type and clean it up when dropped. cmdr has no resource value kind, and adding one costs more than the check is worth.

## Design

### Module API

```
agent.start ARGS     spawn the command line ARGS, CMD DIR ...; return its id as text
agent.result ID      block until the run ends; return its output, or fail with why
agent.wait IDS       block until each run ends; fail unless all are done
agent.state ID       running, done, failed, cancelled, or lost
agent.cancel ID      SIGTERM the run's process group, then SIGKILL; return its state
agent.runs           every run id, oldest first
agent.status IDS     one line per run in IDS, or per run if IDS is empty
agent.log ID         the run's log, then its current step's output so far
```

ARGS is a list, since a block takes a fixed number of arguments. CMD is `ask`, `edit`, `chain`, `audit`, `fix`, or `build`, as `agent.cli` takes it. The blocking calls work as before.

```
agent.start $(list edit ~/projects/hilda claude "write a review") | as r
agent.result $r | lines | keep { contains TODO }

each $dirs { (d) agent.start $(list ask $d claude summarise) } | as runs
each $runs { (r) agent.result $r }
```

`agent.result` and `agent.wait` poll `state` with `retry 604800 every 1`, at most a week. `retry` sleeps in 50 ms slices and checks for an interrupt between them, so Ctrl-C stops a wait.

### CLI

`agent.cmdr start`, `status`, `log`, `result`, `wait`, and `cancel` call the module command of the same name and print its value. For a live view, `watch -n1 agent.cmdr status` is enough. A curses viewer can come later as its own program, reading the same files.

### Run directory

`agent.start` checks CMD and DIR, makes `$AW_RUNS/ID/`, and spawns `env AW_RUN_ID=ID cmdr agent/run.cmdr ARGS`. ARGS go through argv, so a prompt with newlines needs no encoding. `run.cmdr` sits in the package, beside `actions/`, so it does not depend on where `agent.cmdr` is installed. `AW_CMDR` names the cmdr to start; cmdr cannot name its own executable.

| File | Written by | Content |
|-|-|-|
| `dir` | start | DIR, links resolved |
| `argv` | start | ARGS joined with spaces, for display |
| `started` | start | epoch seconds |
| `pid` | start | the run's pid, which is also its session and group id |
| `state` | both | `running`, then `done`, `failed`, or `cancelled` |
| `log` | run | the run log, with each `## STEP` heading written before its agent starts |
| `out`, `err` | run | the current step's stdout and stderr, written as the agent runs |
| `result` or `error` | run | the output, or why the run failed |
| `run.err` | run | cmdr's own stderr, such as an interrupt's message |

Every file is replaced by a rename, so a reader never sees part of one. The run writes its own state, so no daemon is needed. `state` reports `lost` when the file says `running` but `ps -o stat=` finds no process, or a zombie, for the pid. It reads the file again after that check, since the run may have finished between the two reads. Pid reuse can make a lost run look live; nothing checks for it.

`out` and `err` hold only the current step; earlier steps' outputs are in `log`. claude runs with `--output-format stream-json --verbose`, so `out` gains a line per event. `agent.log` renders text and tool calls with jq and passes other agents' lines unchanged.

`AW_RUNS` defaults to `~/.cmdr/runs`, so that `status` works from any directory. This also moves the blocking calls' logs.

### Core changes

1. **`spawn OPTIONS PROGRAM [ARG ...]`.** It starts a program and returns its pid without waiting. Before each spawn, it reaps the exited children it started: `waitpid(pid, WNOHANG)` over a list of their pids. It uses posix_spawn with `POSIX_SPAWN_SETSID` (glibc 2.26, macOS 10.15, POSIX.1-2024), with stdin from `/dev/null`. glibc declares that flag only under `_GNU_SOURCE`, so `src/primitives.c` defines its value on Linux. The options are `dir`, `out PATH`, and `err PATH`. `setsid -f` from util-linux would need no C, but macOS has no `setsid(1)`.

2. **`run-with` and `run-all` gain `out PATH` and `err PATH`.** These replace the temporary files with named ones, opened for append, so a reader can follow them and both may name one file. The result is read back from PATH, so callers do not change.

Everything else is in cmdr, in `agent.cmdr` and `agent/__init__.cmdr`.

## Failure modes

- **Two write runs on one directory.** Their edits would interleave. `start` refuses any command but `ask` while a live run has the same `dir`. It scans the run directories rather than taking a lock, so two starts at the same moment can both pass.

- **Auto mode unattended.** A detached `edit` in auto mode can run Bash with no one watching. auto stays the default; the README says so.

- **Zombies in a long-lived session.** A REPL's spawned runs are its children. `spawn` reaps exited ones, so at most the runs that ended since the last spawn remain, and `state` treats them as gone. `waitpid(-1)` would also reap a child that `run-with` waits on in another thread of a host.

- **Interrupts.** Ctrl-C in the shell or REPL does not reach a detached run. `cancel` is the only way to stop one. Runs survive the REPL's exit; a session that wants them stopped calls `agent.cancel`. `cancel` signals only a run whose state is `running` and whose pid `ps` finds; a lost run's reused pid would still be signalled.

- **Disk.** Runs accumulate. `status` lists them all; pruning is left to `rm` until it is a problem.

## Open questions

- Notification when a run ends: terminal bell, `notify-send`, or nothing. Nothing, until `status` has been used for a while.

- Adapters other than claude: codex and cursor have their own streaming formats. `log` shows their raw output until a filter is needed.

- A `cmdr-exe` primitive, from `/proc/self/exe` or `_NSGetExecutablePath`, would replace `AW_CMDR`.
