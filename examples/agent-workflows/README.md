# Agent workflows

Run coding agents on a project with cmdr. You can send one prompt, or run a chain of steps where each step names an action, an agent, and an optional model.

| File | Contents |
|-|-|
| [aw.cmdr](aw.cmdr) | Command-line entry point |
| [aw/](aw/__init__.cmdr) | The `aw` module: adapters, targets, steps, chains, and the default workflows |
| [aw/actions/](aw/actions) | One prompt template per action |
| [demo.cmdr](demo.cmdr) | Fixes the bug in `sample/` with one agent |
| [sample/](sample) | A small Python project with a reported bug in `BUG.md` |
| [tests/](tests) | Checks every entry point of `aw` with the `dry` agent, and the `claude` and `llama` adapters with stub binaries |

```sh
AW_RUNS=/tmp/aw-runs ./build/cmdr examples/agent-workflows/demo.cmdr                # dry run
AW_RUNS=/tmp/aw-runs ./build/cmdr examples/agent-workflows/demo.cmdr claude:sonnet  # real run
```

The demo copies `sample/` into `$AW_RUNS/demo-project` and commits it. It then asks the agent about the project and runs `reproduce`, `diagnose`, `patch`, and `review` on `BUG.md`. Last, it prints `git status` and the exit status of the sample's tests. Without an argument every step uses `dry`, which prints its prompt and changes nothing.

CTest runs `tests/test_workflows.cmdr`, the demo, and `aw.cmdr`, all with `dry`. It runs `tests/test_llama.cmdr` and the claude preflight with `tests/stubs/` first on `PATH`.

## Using the module

`import aw` binds the module's definitions as `aw.NAME`. `demo.cmdr` and `aw.cmdr` find `aw/` next to them. A script elsewhere finds it through `CMDR_PATH`, or through `~/.cmdr/lib`:

```sh
export CMDR_PATH=~/src/cmdr/examples/agent-workflows      # or:
ln -s ~/src/cmdr/examples/agent-workflows/aw ~/.cmdr/lib/aw
```

The module finds `aw/actions/` through `script-dir`, so it works wherever it is installed.

## Command line

`aw.cmdr` runs with `cmdr` on `PATH`. Each subcommand calls the entry point of the same name. Steps given to `audit`, `fix`, or `build` replace the workflow's default steps.

```sh
aw.cmdr ask ~/src/app claude:haiku where is the lexer
aw.cmdr edit ~/src/app codex rename parse to parse_line
aw.cmdr chain ~/src/app survey@claude review@codex:gpt-5
aw.cmdr audit ~/src/app
aw.cmdr fix ~/src/app bug-142.md
aw.cmdr build ~/src/app spec.md plan@claude:opus implement@cursor verify@codex
```

The words after AGENT form the prompt. A missing argument or an unknown subcommand prints the usage and exits with status 1.

| Variable | Default | Meaning |
|-|-|-|
| `AW_RUNS` | `agent-runs` | Directory for run logs |
| `AW_TIMEOUT` | `1800` | Seconds before an agent is stopped |
| `AW_LLAMA_URL` | `http://127.0.0.1:8080` | llama-server base URL |
| `AW_LLAMA_MAX_BYTES` | `200000` | Largest project text the `llama` agent sends |

## Model

Names below are the module's own; an importer writes `aw.NAME`.

- **Target**: a list `KIND DIR BRIEF`. KIND is `path`, `bug`, or `feature`. DIR is the agent's working directory. BRIEF opens every prompt. `target` rejects a DIR that is not a directory.

  | Constructor | BRIEF holds |
  |-|-|
  | `target-path DIR` | Only the scope |
  | `REPORT \| target-bug DIR` | The bug report |
  | `SPEC \| target-feature DIR` | The feature specification |

- **Action**: `aw/actions/NAME.md`. Line 1 is `mode: read` or `mode: write`. The rest is a template with three placeholders:
  - `{{target}}`: the target's BRIEF.

  - `{{input}}`: the previous step's output, or `(none)`.

  - `{{history}}`: every earlier step's output, under `## STEP` headings.

- **Step**: `ACTION@AGENT[:MODEL]`, such as `plan@claude:opus` or `review@cursor:gpt-5`. MODEL may contain `:` and `/`. If MODEL is omitted, the agent uses its default.

- **Chain**: `chain TARGET STEPS` runs STEPS in order and returns the last output. `chain-from TARGET STEPS INPUT` passes INPUT to the first step.

- **Workflow**: a chain with default steps for one target kind. It fails on any other kind. `workflow TARGET KIND STEPS` runs other steps with the same check.

  | Workflow | Kind | Steps |
  |-|-|-|
  | `audit` | path | `survey@claude review@codex report@claude` |
  | `fix` | bug | `reproduce@claude diagnose@codex patch@claude review@codex` |
  | `build` | feature | `plan@claude critique@codex implement@claude verify@codex review@cursor` |

- **Prompt**: `ask TARGET AGENT[:MODEL] PROMPT` sends one read-only prompt. `edit` does the same in write mode. Each is a one-step chain, using `aw/actions/ask.md` or `aw/actions/edit.md`.

```text
cmdr> import aw
cmdr> aw.target-path . | with { (t) aw.ask $t claude:haiku "Where is the lexer?" }
cmdr> read bug-142.md | aw.target-bug ~/src/app | aw.fix
cmdr> read spec.md | aw.target-feature ~/src/app | with { (t) list plan@claude:opus critique@codex:gpt-5 implement@cursor verify@codex | with { (s) aw.chain $t $s } }
```

In the REPL, `import` searches the current directory first; run it from `examples/agent-workflows`, or set `CMDR_PATH`.

`chain` parses and checks every step before any agent runs. It checks that the step matches the grammar, that the agent is known, and that the action file exists with a valid mode. A typo in step 4 costs no agent time on steps 1 to 3. An agent that exits nonzero or times out stops the chain, with the last 20 lines of its stderr in the error. Each run also writes `$AW_RUNS/STAMP-XXXXXX`, with each `## STEP` heading written before its agent starts.

## Agents

An adapter is `agent-NAME PROMPT DIR MODEL MODE`. `agent NAME ...` dispatches to it through `map`, which looks up a command name at run time. The table below shows what each adapter passes. Flags were checked against `--help` of the installed versions on 2026-10-19. The argv for each mode was checked with stub binaries. The sandbox flags follow each agent's documentation and have not yet been tested against a live agent.

| Agent | Version | Command | `read` | `write` |
|-|-|-|-|-|
| `claude` | 2.1.303 | `claude -p --permission-prompts none --safe-mode --settings SANDBOX` | `--permission-mode plan` | `--permission-mode acceptEdits` |
| `codex` | 0.163.0 | `codex exec` | `-s read-only` | `-s workspace-write` |
| `cursor` | 2026.10.16 | `cursor-agent -p --output-format text --sandbox enabled` | `--mode=plan` | `--force` |
| `llama` | llama.cpp b11669 | `curl` to `/v1/chat/completions` | returns the reply | applies the reply's edit blocks |
| `dry` | | none | returns the prompt | returns the prompt |

Every agent that runs commands does so in its own sandbox, so agents without one are not adapted:

- `claude`: SANDBOX is `claude-sandbox`, Claude Code's sandbox for Bash. Bash may write only in DIR and a temp directory, and a blocked command is never retried unsandboxed. The sandbox does not cover the Edit and Write tools; `acceptEdits` accepts edits in DIR, and `--permission-prompts none` refuses anything else. `--safe-mode` skips CLAUDE.md files, including `~/.claude/CLAUDE.md`, but keeps the login.

  On Linux, each step first runs `bwrap --unshare-net` and fails with its stderr if that fails. Ubuntu 24.04's `kernel.apparmor_restrict_unprivileged_userns=1` makes bwrap fail with `loopback: Failed RTM_NEWADDR`. Claude Code's `failIfUnavailable` misses this: claude exits 0 after every Bash call has failed. To fix the host, give `/usr/bin/bwrap` an AppArmor profile that allows `userns`.
- `codex`: `-s read-only` or `-s workspace-write`.
- `cursor`: `--sandbox enabled`.
- `llama`: runs no commands. It sends every file from `git ls-files` in DIR, then the prompt, to llama-server, and fails if the text exceeds `AW_LLAMA_MAX_BYTES`. MODEL becomes the request's `model`. Every prompt says it cannot run commands, since the action templates ask it to run tests. `reproduce` and `verify` steps only write code.

  In write mode the reply gives each change as an edit block, the format aider uses:

  ```text
  stats.py
  <<<<<<< SEARCH
      return sum(xs) / (len(xs) - 1)
  =======
      return sum(xs) / len(xs)
  >>>>>>> REPLACE
  ```

  SEARCH must match whole lines at exactly one place in the file; an empty SEARCH creates a file. A block without a path line uses the previous block's path, or else the one tracked file that contains its SEARCH lines. Edit blocks over unified diffs, because Qwen3-Coder-30B-A3B wrote diffs that `git apply` rejected in 4 of 6 live steps: hunks that ended without context, or repeated a line. A path is rejected if it is absolute, has a `.`, `..`, or `.git` component, or passes through a symbolic link. Blocks apply in order to a copy in `AW_RUNS`, and DIR changes only if every block applies. The copy is deleted after a success and kept after a failure. Otherwise the step asks once more with the reasons; a second failure fails the step, with the reasons and the reply in the message.

Each adapter builds an argument list and calls `launch`, which runs it with `run-with` in DIR under `AW_TIMEOUT`. No shell is involved, so a prompt with quotes or `$` is passed unchanged. `opt FLAG VALUE` gives an empty list for an empty VALUE, which `run-with` drops. Add an agent with one `def agent-NAME` and its name in `agents`.

The action declares the mode, not the step, so a `review` cannot get write access.

Design decisions and open questions: [docs/dev/design.md](docs/dev/design.md).
