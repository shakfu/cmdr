# Concurrency and asynchrony

Design, 2026-10-21. Status: SIGTERM handling and A (`run-all`) implemented; B to E not. The current model is in [concurrency.md](concurrency.md).

## Problem

A script waits for each program before it starts the next. Agent workflows spend nearly all their time in programs: one `aw` step runs an agent for minutes, while cmdr's own commands take microseconds. This is inferred from `aw`'s design, not measured. Asking three agents the same question takes the sum of their times, not the longest.

Today's workaround loses structure: `run sh -c 'a & b & wait'` returns one combined stdout, with no per-program status.

`aw`'s chains pass each step's output to the next (`examples/agent-workflows/aw/__init__.cmdr:108`), so one chain cannot overlap. The need is fan-out. The main case is one workflow on several projects, such as `audit` on every repository in a directory; see [One workflow on several projects](#one-workflow-on-several-projects). Smaller cases are one prompt to several agents, and one render per input file.

## Constraints in the current code

1. A block cannot cross threads. Copies share compiled code with a plain `size_t` reference count (`src/cmdr.c:205`), and its scope points into the session that wrote it.

2. `def`, `as`, and `import` change the session's command table while lines run. Another thread reading that table would race.

3. Values are whole and owned. `evaluate` finishes a stage's value before the next stage starts (`src/cmdr.c:725`). There is no lazy or streaming value kind.

4. Before `run-all`, `run` waited for one child (`wait_until` in `src/primitives.c`). I/O goes through temporary files, so no pipe can fill and block.

5. Interrupts are process-wide. One Ctrl-C should stop every program a line started.

6. Extensions assume one call at a time per thread. `snd` holds a global lock. `random` keeps one generator per thread, so a seeded script gives different draws if its blocks move between threads.

## Options

| | Option | Runs in parallel | Change | Risk |
|-|-|-|-|-|
| A | `run-all`: run several programs, wait for all | programs | one command, a multi-child wait | low |
| B | `start` and `wait-job`: handles to running programs | programs, and cmdr between them | per-session job table | medium |
| C | Parallel `each` over blocks, on threads | cmdr code | atomic counts, frozen session, extension audit | high |
| D | Streaming values between stages | nothing; overlaps producer and consumer | a new value kind in every list command | high |
| E | Event loop with `async` and `await` | cmdr code, cooperatively | a new evaluator | very high |

### A. `run-all`

Run a list of jobs at once, at most N at a time, and return each job's `STATUS STDOUT STDERR` in input order. Each job is `OPTIONS ARGV`, with the options `run-with` takes.

```text
cmdr> list codex cursor claude | each { (a) list $(list stdin $q timeout 600) $(agent-argv $a) } | run-all 3
```

`agent-argv` stands for a definition that returns an agent's command line; `aw` has none today.

- Results are data, as for `run-with`, so one failed job does not discard the others. The caller decides with `keep` or `each`.

- Order follows the input, not the finish time, so results do not depend on timing.

- Captured stderr keeps programs from interleaving on the terminal.

- An interrupt sends SIGTERM to every running job, then SIGKILL a second later. Jobs not yet started do not start. The stage fails with `interrupted`.

- Each job's timeout runs from its own start.

Implementation:

- Split `execute` into a spawn step and a collect step.

- Generalise `wait_until` to many children: one pidfd per child in one `poll` on Linux, one kqueue with an `EVFILT_PROC` per child on BSD and macOS, and the 10 ms `WNOHANG` poll elsewhere.

- Each running job holds three temporary files and one pidfd. N bounds the descriptors.

- No threads, and no change to values, sessions, or extensions.

### B. Job handles

`start OPTIONS PROGRAM ...` returns a handle. `wait-job HANDLE` returns `STATUS STDOUT STDERR`. cmdr can run other commands between the two.

- A handle is text, since cmdr has no opaque value kind. It can be copied, forged, or waited twice; each case needs an error.

- A line that fails between `start` and `wait-job` leaves a running job. The session must kill and reap it, at the end of the line or at `cmdr_session_close`.

- `try` does not catch interrupts, so an interrupt must also kill every job.

- B covers A: `each { ... start } | map wait-job`. It costs a job table and lifetime rules that A does not need.

### C. Parallel blocks

`each-parallel N LIST BLOCK` runs BLOCK per item on N threads.

- `cmdr_code.refs` becomes atomic, or each thread gets its own copy of the block.

- The session's table is frozen while threads run; `def`, `as`, and `import` inside the block fail.

- Every extension must be safe to call from several threads, or say it is not. `random.seed` stops being reproducible.

- Threads need a stack for 128 levels of calls. Secondary threads get 512 KB by default on macOS, against 8 MB on Linux.

- The gain is small: cmdr's own commands are fast, and A already runs programs in parallel.

### D. Streaming

A lazy list kind, so `read` and `run` could yield lines as they arrive. Every list command would need to accept it, and errors would surface mid-stream after earlier stages had acted. `docs/dev/scenarios-of-use.md:104` notes that logs larger than memory do not work, which is the case for this. It is a separate design. `run sh -c 'a | b'` already streams between programs.

### E. Async evaluator

`evaluate` would become resumable, with each command able to yield. This rewrites the core for a capability that A and B give with one or two commands.

## One workflow on several projects

A workflow is cmdr code, not one program. `chain-from` folds over the steps, and cmdr code applies edits between agents (`aw/__init__.cmdr:109`, `:56`). There are three ways to run it on N projects.

| | How | Verdict |
|-|-|-|
| 1 | One cmdr process per project, as A's jobs | Yes |
| 2 | One process in lockstep: step k for every project through A, then step k + 1 | No: `aw` must be rebuilt around batches, the llama adapter runs three programs per step (`:37`), and the slowest project holds up every step |
| 3 | One session, a thread per project, as C | No: every cost of C |

With 1, each project gets its own session, so no code needs to be thread-safe:

```text
cmdr> list ~/src/a ~/src/b ~/src/c | each { (d) list $(list group 1 timeout 7200) $(list aw.cmdr audit $d) } | run-all 3
```

This works today without new code: `printf '%s\n' ~/src/a ~/src/b ~/src/c | xargs -P 3 -I{} aw.cmdr audit {}`. A adds, over `xargs`:

- each project's status and output as a cmdr value, not one exit code (123 if any failed) and mixed output;

- a timeout per project;

- results in input order, so a script can report or chain on them.

Requirements this case adds:

- **The whole process tree must stop.** SIGTERM ends a cmdr process but not the program it is waiting for. Tested: after `kill -TERM` on `cmdr -c 'run sleep 7.77'`, `sleep` kept running. A timed-out project would leave its agent editing the repository. Either each job uses `group 1`, so the signal reaches the agent, or `src/main.c` treats SIGTERM as it treats SIGINT, so a child cmdr stops its own program. The second also fixes `run-with timeout` on any cmdr script.

- **Shared services queue requests.** One llama-server serves every project, and handles requests one at a time unless started with parallel slots (`--parallel`; from llama.cpp's options, not tested here). Hosted agents have rate limits, and cost grows with N.

- **No project twice.** Two agents in one directory edit the same tree. Reject duplicates after resolving paths.

- Already safe: `run-log` and `edit-apply` make their files with `mktemp` (`:106`, `:56`), so concurrent runs do not collide. Agents run non-interactively (`claude -p`, `codex exec`), so losing the terminal under `group 1` does not matter.

## Recommendation

1. Done: SIGTERM stops a cmdr process as SIGINT does. Parallel runs are unsafe without it or `group 1`.

2. Done: A, for one workflow on several projects, run as one cmdr process per project. It keeps results ordered and timing out of results, and adds no state that outlives a command. Try `xargs -P` first if per-project results as values are not needed.

3. B only if a script must run cmdr code while programs run, such as showing progress. Check first whether A plus `run-with` covers it.

4. Not C, D, or E for concurrency. Revisit D as part of large-file support, not as part of this design.

A different view: parallelism is a property of a host, not of the language. A host that wants parallel workflows can run one session per thread today. Under that view, cmdr adds nothing, and a script splits its work across several cmdr processes. This costs startup per process and leaves no shared values between them.

## Open questions

- Should `aw` gain an entry point for this, such as `aw.cmdr audit-all N DIR ...`, or should users write the `run-all` line?

- Should a child cmdr be found on `PATH`, as `aw.cmdr`'s shebang assumes, or should cmdr expose its own executable path?

- Should `run-all` gain a fail-fast option, killing the other jobs when one exits nonzero? As built, a nonzero status is data, as for `run-with`, and only a job that cannot start fails the stage.
