# io

Read stdin and write stdout or stderr while a line runs. A line's result prints only when the line ends, so these commands are the way to show progress or matches as they arrive, or to ask for input.

| Command | Result |
|-|-|
| `io.input [PROMPT]` | One line of stdin, without its `\n`. PROMPT, if given, goes to stderr first. End of input is a mismatch. |
| `io.print VALUE` | Writes VALUE to stdout, as a line's result prints, and flushes. Returns nothing. |
| `io.tee VALUE` | As `io.print`, but returns VALUE. |
| `io.eprint VALUE` | As `io.print`, to stderr. |
| `io.etee VALUE` | As `io.tee`, to stderr. |

`each`, `fold`, and `fs.each-line` fail when a block returns nothing, so `io.print` belongs at the end of a line. Inside a block, use `io.tee`. To print each match of a log as it arrives without printing the list again at the end, count the matches:

```text
cmdr> import fs
cmdr> import io
cmdr> fs.each-line - { (l) matches $l ERROR | io.tee } | count
```

`io.etee` reports progress without changing what a pipeline passes on:

```text
cmdr> run curl -s https://example.com/data.json | io.etee | json.keys
```

Python's `input` writes PROMPT to stdout. This module writes it to stderr, as Bash's `read -p` does, so the prompt stays out of output that is piped or redirected. Like `fs.each-line`, `io.input` splits at `\n` only and keeps a `\r`. The commands write to the process's stdout and stderr, even in a host that sends line results elsewhere.
