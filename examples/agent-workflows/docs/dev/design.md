# Agent workflows design

Decision record, 2026-10-19. Primitives added for this example are analyzed in [docs/dev/run-with-script-push.md](../../../../docs/dev/run-with-script-push.md).

## Decisions

| Choice | Over | Because |
|-|-|-|
| The action declares read or write | A per-step flag, `review@claude!write` | A step author could give write access to any action, and nothing would check it |
| Templates in `aw/actions/*.md` | One `def` per action | Prompts inside cmdr quoting need escapes for every `'` and `{` |
| `{{input}}` and `{{history}}` | Only the last output | `implement` needs the plan, which is two steps back |
| `{{history}}` on request | Whole history in every prompt | Every prompt would grow with the length of the chain |
| History as a flat list, `## STEP` then output | A list of pairs | `nth` cannot return a list item |
| Steps checked before the first agent | Checking each step as it runs | A typo in step 4 would cost three agent runs |
| `ACTION@AGENT[:MODEL]` text | A list per step | A chain is one `list` of words; MODEL may hold `:` and `/` |
| Workflows check the target kind | Any target for any workflow | `fix` on a path target would give `reproduce` no bug report |
| Only agents with a built-in sandbox: claude, codex, cursor | Also pi and opencode, unconfined | A live run installed `pytest` into the system Python. A container ([sanduk](https://github.com/shakfu/sanduk)) would confine every agent, but needs API keys and a container engine; it is the route back for pi and opencode |
| Adapters call `run-with` directly | `sh -c` per adapter | No shell quoting, and stderr and the timeout reach the caller |
| `ask` and `edit` as one-step chains | Separate code paths | One path to log, check, and test |
| A package, `aw/__init__.cmdr` | A file loaded by path | Importers do not depend on where the example lives; see [docs/dev/import.md](../../../../docs/dev/import.md) |
| `aw.cmdr` subcommands dispatch through `map` to `aw-NAME` | One `def` that checks each name in turn | cmdr has no conditional; `map` looks up a name at run time, as `agent` does |
| Steps after the fixed arguments replace the defaults | A separate `--steps` flag | No flag parsing; a workflow's arguments are fixed in number, so the rest are steps |
| The prompt is every word after AGENT | One quoted argument | `aw.cmdr ask . claude where is it` needs no quotes; a quoted prompt still works |

## History

The first sketch worked around three cmdr gaps:

- Each adapter ran its agent through `sh -c 'cd "$1" ...'`, since `run` had no working directory. Stderr went to the terminal, and a failure reported only `exited with status N`.

- `AW_HOME` named this directory, defaulting to a path relative to the repository root.

- `{{history}}` re-read the run log, since `fold` could not collect outputs in a list.

`run-with`, `script-dir`, and `push` replaced them. Later, `import aw` replaced loading `workflows.cmdr` by a path relative to the importer.

## Verification

- Agent flags were checked against `--help` of the installed versions on 2026-10-19. The versions are listed in the [README](../../README.md#agents).

- The argv for each agent and mode was checked with stub binaries on `PATH`. Stubs also checked a nonzero exit (stderr in the error) and a timeout.

- No live agent was run.

## Open questions

1. **Isolation.** Write steps edit DIR in place. A chain could run in a `git worktree` and return a branch. Is that a target kind or a chain option?

2. **Gates.** `verify` asks an agent to run the tests. A non-agent step, such as `gate@sh:make test`, would give the same result every run and cost nothing. That needs a step kind beyond ACTION@AGENT.

3. **Parallel steps.** "Review with codex and cursor, then merge" needs a fan-out. cmdr runs stages one at a time, so this needs a C extension.

4. **Structured output.** Steps pass free text. `claude -p --output-format json` and `cursor-agent --output-format json` could pass a verdict field, which a gate could fail on.

5. **Cost and retries.** There is no budget, retry, or resume. `chain-from` with the last output as INPUT can resume by hand.

6. **Target kinds.** The kinds differ only in BRIEF and in which workflow accepts them. A target could instead be a directory plus context documents, with the workflow naming the documents it needs. That removes the kinds but loses the early kind check.
