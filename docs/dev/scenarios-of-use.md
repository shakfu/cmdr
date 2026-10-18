# Scenarios of use

Where embedding cmdr in another program, or building a domain-specific language (DSL) on it, would be useful, and what blocks each case today. Written 2026-10-18 against the working tree on `2f49ed2`. Every cmdr example marked "Output" was run against that build; C sketches were not compiled.

## Terms

- **Embedding**: a host program adds its own C commands and runs cmdr lines in its process. Today this means editing the `commands` table in `src/main.c`. There is no library target: [minimal.md](minimal.md) removed `libcmdr.a` and the embedding API.

- **DSL**: a domain's vocabulary written as `def`s over the primitives, in a file run with `load` or `--user`. It needs no C.

## What cmdr offers either use

1. A whole line compiles before any stage runs. Unknown commands, bad references, and wrong block arity fail before the first effect.

2. Values are typed. A list stays one argument; there is no re-splitting of text, unlike Tcl or the shell.

3. Mismatch (status 2) is separate from error (status 1). `keep`, `not`, `or`, and `check` act only on mismatch, so a typo is never read as false.

4. A script stops at its first failing line and reports `FILE:LINE:`.

5. Mismatch messages name the call and its values: `ends-with ar bar: ge: 2 is not at least 3`.

## Embedding scenarios

### E1. Debug console in a game or simulation

The host adds `entities`, `get ID FIELD`, and `set ID FIELD VALUE`. A developer types:

```text
entities | keep { (e) get $e hp | lt 10 } | each { (e) set $e hp 100 }
```

- Fit: point 1 means a typo in `set` runs no `get`. Point 2 means entity lists need no quoting.

- Blocks:

  - `run()` in `src/main.c` owns the input loop and blocks on `getline`. A game loop needs "evaluate this line" without the loop. `cmdr_eval` already does that, but `main.c` holds the session as a file-scope `static`.

  - `cmdr_realloc` calls `exit(1)` on allocation failure. A host cannot recover.

  - `cmdr_call` counts depth in a function-scope `static`. Two threads evaluating at once corrupt it.

- Verdict: useful once there is a library target. It needs a session object in place of the `static`, and the allocation-failure policy has to be the host's choice.

### E2. Admin console for a server

The host adds `keys PREFIX`, `get KEY`, and `del KEY`. An operator cleans up:

```text
keys session: | keep { (k) get $k | matches '"expired":true' } | each { del }
```

- Fit: point 1 and point 3. A wrong predicate errors instead of deleting every key.

- Blocks:

  - `run`, `run-in`, `read`, `write`, and `append` are always present. Operators of a server console should not get a shell or file access. The host needs a primitive table without them.

  - A remote console needs per-connection sessions; see E1.

- Verdict: useful, if the host can choose which primitives to include.

### E3. Hardware bring-up and test benches

The host adds `reg-read ADDR` and `reg-write ADDR VALUE`. Test scripts run with `load`:

```text
reg-write 0x40 1
reg-read 0x44 | check { equals 1 } "PLL did not lock"
```

- Fit: point 4 stops at the first failing step and prints its line. `check` gives the failure a domain message.

- Blocks: numbers are C `double`s, printed with `%.17g`.

  - Hex input is accepted, but output is decimal: `add 0x10 1` outputs `17`.

  - Integers above 2^53 lose precision: `add 9007199254740993 0` outputs `9007199254740992`. 64-bit registers are not safe.

- Verdict: useful for registers of 32 bits or fewer. Wider registers need integer arithmetic, or the host's commands must keep values as text.

## DSL scenarios

### D1. Log triage

Needs only the prelude. With `app.log` holding five lines such as `2026-10-18 10:00:02 ERROR db timeout`:

```text
grep app.log ERROR | count
read app.log | keep { matches ' ERROR ' } | each { words | nth 4 } | distinct | unwords
read app.log | keep { matches 'slow [0-9]+ms' } | each { words | last | strip-suffix ms } | max
```

Output: `3`, `cache db`, `850`.

- Fit: `matches`, `words`, `nth`, `distinct`, and `max` compose without helper code.

- Blocks:

  - `read` loads the whole file. Logs larger than memory do not work, and there is no streaming or follow mode.

  - `matches` only tests. There is no capture group or regex replace, so a field is reached by position (`nth 4`) rather than by pattern.

- Verdict: useful today for files that fit in memory. A capture primitive, such as `match TEXT REGEX` returning its groups as a list, would remove most positional code.

### D2. Small tabular data

With `data.csv` holding `name,qty,price`, then `apple,3,0.5` and `pear,2,1.25`:

```text
read data.csv | lines | rest | each { split , | nth 2 } | sum
read data.csv | lines | rest | each { split , | with { (r) nth $r 2 | with { (q) nth $r 3 | mul $q } } } | sum
```

Output: `5` (total quantity), `4` (total value).

- Fit: `rest` drops the header, and `with` reaches two columns of one row.

- Blocks:

  - `split ,` does not handle quoted fields (`"a,b"`).

  - Two columns need two nested `with` blocks. A `def col { (r n) ... }` hides this, but each extra column adds a level of nesting.

- Verdict: useful for simple, machine-written CSV. Quoted CSV needs a C primitive, and it should be added only if real data needs it.

### D3. Build and release recipes

A project prelude defines its steps:

```text
def object { (f) stem $f | with { (s) list $s .o | concat } }
files src | keep { ends-with .c } | each { (f) object $f | with { (o) run echo cc -c $f -o $o } } | concat
```

Output (with `echo` in place of the compiler): `cc -c src/a.c -o a.o`, then `cc -c src/b.c -o b.o`.

- Fit: point 1 checks the whole recipe before the first compile. A nonzero exit fails the stage and stops a script.

- Blocks:

  - The child's stderr goes to cmdr's stderr, and a failure reports only `run sh: exited with status 3`. A build log needs the stderr as a value.

  - Steps run one at a time. There is no dependency tracking or out-of-date check, which `make` provides.

  - There is no environment access except `run printenv NAME`.

- Verdict: useful for release steps that are not builds (tag, package, upload). For compiling, `make` or `ninja` is the better tool; cmdr adds nothing over them.

### D4. Repository and config checks in CI

Assertions over files, with exit status 1 on failure:

```text
files src | all { (f) basename $f | matches '^[a-z]+\.[ch]$' } | count
files src | all { (f) basename $f | matches '^[a-z]\.c$' } | count
```

Output: `3`. The second line fails:
`all [src/a.c, src/b.c, src/util.h] { (f) basename $f | match...}: empty: not: [src/util.h] satisfies { nonempty }`.

- Fit: point 4 and point 5. The failure names the file that broke the rule.

- Blocks:

  - A `check` inside `all`'s block is not shown. `keep` treats the mismatch as false, and `all` then reports its own `empty: not:`.

  - Block source in the message is cut at 32 bytes (`match...}`).

- Verdict: useful today. If these messages are read often, collapsing helper names for mismatches (change 3, not done) would shorten the second line.

## Gaps across scenarios

| Gap | Scenarios | Smallest change |
|-|-|-|
| No library target; session is a `static` in `main.c` | E1, E2 | Move the session into a struct that `cmdr_eval` takes |
| `exit(1)` on allocation failure | E1, E2 | Host-supplied failure hook |
| Call depth in a `static` | E1, E2 | Depth in the session struct |
| All primitives always present | E2 | Split `cmdr_primitives` into pure and effect tables |
| `double` numbers | E3 | Integer path in `arithmetic` when both operands are integers |
| No regex capture | D1 | `match TEXT REGEX` returning groups |
| `run` stderr not captured | D3 | `run` variant returning stdout and stderr |
| Whole-file `read` | D1 | Line-at-a-time reading; changes the value model, so last |

The first three are one change: a session object. Each DSL scenario works today within its stated limits, and none needs that change.

## Alternative tools

Tcl and Lua are mature options for embedding. cmdr differs on points 1 to 3: it checks before running, keeps values typed, and separates mismatch from error. It lacks their libraries, their sandboxing, and a non-exiting allocation policy. Choose cmdr for short command lines typed by people, where checking before effects matters. Choose Lua for anything that needs a general-purpose language.
