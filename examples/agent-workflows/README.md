# Agent workflows

Run coding agents on a project with cmdr. You can send one prompt, or run a chain of steps where each step names an action, an agent, and an optional model.

| File | Contents |
|-|-|
| [aw.cmdr](aw.cmdr) | Command-line entry point |
| [workflows.cmdr](workflows.cmdr) | Adapters, targets, steps, chains, and the default workflows |
| [actions/](actions) | One prompt template per action |
| [demo.cmdr](demo.cmdr) | Fixes the bug in `sample/` with one agent |
| [sample/](sample) | A small Python project with a reported bug in `BUG.md` |
| [tests/](tests) | Checks every entry point of `workflows.cmdr` with the `dry` agent |

```sh
AW_RUNS=/tmp/aw-runs ./build/cmdr examples/agent-workflows/demo.cmdr                # dry run
AW_RUNS=/tmp/aw-runs ./build/cmdr examples/agent-workflows/demo.cmdr claude:sonnet  # real run
```

The demo copies `sample/` into `$AW_RUNS/demo-project` and commits it. It then asks the agent about the project and runs `reproduce`, `diagnose`, `patch`, and `review` on `BUG.md`. Last, it prints `git status` and the exit status of the sample's tests. Without an argument every step uses `dry`, which prints its prompt and changes nothing.

`workflows.cmdr` finds `actions/` through `script-dir`, so it loads from any directory. CTest runs `tests/test_workflows.cmdr`, the demo, and `aw.cmdr`, all with `dry`.

## Command line

`aw.cmdr` runs with `cmdr` on `PATH`. Each subcommand calls the entry point of the same name. Steps given to `audit`, `fix`, or `build` replace the workflow's default steps.

```sh
aw.cmdr ask ~/src/app claude:haiku where is the lexer
aw.cmdr edit ~/src/app codex rename parse to parse_line
aw.cmdr chain ~/src/app survey@claude review@codex:gpt-5
aw.cmdr audit ~/src/app
aw.cmdr fix ~/src/app bug-142.md
aw.cmdr build ~/src/app spec.md plan@claude:opus implement@cursor verify@pi
```

The words after AGENT form the prompt. A missing argument or an unknown subcommand prints the usage and exits with status 1.

| Variable | Default | Meaning |
|-|-|-|
| `AW_RUNS` | `agent-runs` | Directory for run logs |
| `AW_TIMEOUT` | `1800` | Seconds before an agent is stopped |

## Model

- **Target**: a list `KIND DIR BRIEF`. KIND is `path`, `bug`, or `feature`. DIR is the agent's working directory. BRIEF opens every prompt. `target` rejects a DIR that is not a directory.

  | Constructor | BRIEF holds |
  |-|-|
  | `target-path DIR` | Only the scope |
  | `REPORT \| target-bug DIR` | The bug report |
  | `SPEC \| target-feature DIR` | The feature specification |

- **Action**: `actions/NAME.md`. Line 1 is `mode: read` or `mode: write`. The rest is a template with three placeholders:
  - `{{target}}`: the target's BRIEF.

  - `{{input}}`: the previous step's output, or `(none)`.

  - `{{history}}`: every earlier step's output, under `## STEP` headings.

- **Step**: `ACTION@AGENT[:MODEL]`, such as `plan@claude:opus` or `review@opencode:anthropic/claude-sonnet-4-5`. MODEL may contain `:` and `/`. If MODEL is omitted, the agent uses its default.

- **Chain**: `chain TARGET STEPS` runs STEPS in order and returns the last output. `chain-from TARGET STEPS INPUT` passes INPUT to the first step.

- **Workflow**: a chain with default steps for one target kind. It fails on any other kind. `workflow TARGET KIND STEPS` runs other steps with the same check.

  | Workflow | Kind | Steps |
  |-|-|-|
  | `audit` | path | `survey@claude review@codex report@claude` |
  | `fix` | bug | `reproduce@claude diagnose@codex patch@claude review@codex` |
  | `build` | feature | `plan@claude critique@codex implement@claude verify@codex review@opencode` |

- **Prompt**: `ask TARGET AGENT[:MODEL] PROMPT` sends one read-only prompt. `edit` does the same in write mode. Each is a one-step chain, using `actions/ask.md` or `actions/edit.md`.

```text
cmdr> load examples/agent-workflows/workflows.cmdr
cmdr> target-path . | with { (t) ask $t claude:haiku "Where is the lexer?" }
cmdr> read bug-142.md | target-bug ~/src/app | fix
cmdr> read spec.md | target-feature ~/src/app | with { (t) list plan@claude:opus critique@codex:gpt-5 implement@cursor verify@pi | with { (s) chain $t $s } }
```

`chain` parses and checks every step before any agent runs. It checks that the step matches the grammar, that the agent is known, and that the action file exists with a valid mode. A typo in step 4 costs no agent time on steps 1 to 3. An agent that exits nonzero or times out stops the chain, with the last 20 lines of its stderr in the error. Each run also writes `$AW_RUNS/STAMP-XXXXXX`, with each `## STEP` heading written before its agent starts.

## Agents

An adapter is `agent-NAME PROMPT DIR MODEL MODE`. `agent NAME ...` dispatches to it through `map`, which looks up a command name at run time. The table below shows what each adapter passes. Flags were checked against `--help` of the installed versions on 2026-10-19. The argv for each mode was checked with stub binaries. No live agent was run.

| Agent | Version | Command | `read` | `write` |
|-|-|-|-|-|
| `claude` | 2.1.303 | `claude -p` | `--permission-mode plan` | `--permission-mode auto` |
| `codex` | 0.163.0 | `codex exec` | `-s read-only` | `-s workspace-write` |
| `cursor` | 2026.10.16 | `cursor-agent -p --output-format text` | `--mode=plan` | `--force` |
| `pi` | 1.3.0 | `pi -p` | `--tools read,grep,find,ls` | default tools |
| `opencode` | 2.0.32 | `opencode run` | `--agent plan` | `--auto` |
| `dry` | | none | returns the prompt | returns the prompt |

Each adapter builds an argument list and calls `launch`, which runs it with `run-with` in DIR under `AW_TIMEOUT`. No shell is involved, so a prompt with quotes or `$` is passed unchanged. `opt FLAG VALUE` gives an empty list for an empty VALUE, which `run-with` drops. Add an agent with one `def agent-NAME` and its name in `agents`.

The action declares the mode, not the step, so a `review` cannot get write access.

Design decisions and open questions: [docs/dev/design.md](docs/dev/design.md).
