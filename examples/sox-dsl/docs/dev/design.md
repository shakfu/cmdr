# Sox DSL design

Decision record, 2026-10-19.

| Choice | Over | Because |
|-|-|-|
| Bind libsox | libsoxr for resampling plus effects written here | libsox gives all 62 sox effects with sox's arguments and its tested DSP. Writing filters, reverb, compand, pitch, and noise reduction here is thousands of lines and covers a subset. Homebrew's sox does not use libsoxr; its `rate` has its own resampler |
| `varispeed` as an effect in the libsox chain, on libsoxr | Dropping it, or a separate command | sox has no time-varying speed: `speed` is one factor and `bend` shifts pitch only. As an effect it combines with every other effect, combining, and the splits |
| `varispeed` pads to round(2N/(FROM+TO)) | Its natural length | It reports that length to later effects, which `fade ... -0` needs; soxr gives about 0.1% fewer frames, 58725 where 58800 were expected for 1 to 0.5 |
| `snd.run` takes sox's arguments after its output file | A grammar of our own | sox's documentation and users' commands carry over word for word |
| OUT is `snd.run`'s first argument | sox's order, output between inputs and effects | A chain is built input first and run last; putting OUT first means `snd.out` never inserts into the middle of the list |
| The combiner, auto effects, and output signal ported from `sox.c` | Calling the sox binary with `run` | In-process: no subprocess per call, effects' output is captured by `snd.report`, and errors arrive as cmdr errors |
| `:`, `newfile`, and `restart` are errors; the module splits with `snd.segments` and `trim` | Multiple effects chains in `snd.run` | sox restarts chains on the same open input, which `sox.c` does with process-wide state. Splitting in cmdr renders once and cuts with `trim`, `remix`, or silence positions |
| Commands take paths | Audio as a cmdr value | Values are text without NUL bytes, so samples cannot pass through a pipeline |
| A chain is a list of words | A registry of `snd:N` handles to audio held in C | cmdr has no finalizers, so handles would leak or need explicit freeing |
| Named effects with sox's positional arguments and defaults, plus `effect SPEC` | One `def` per sox effect | Defaults ([docs/dev/defaults.md](../../../../docs/dev/defaults.md)) cover sox's optional positional arguments, such as `reverb`'s six; flags such as `reverb -w` need `effect`, which passes sox's own syntax |
| A separate binary, `cmdr-snd`, configured in `examples/sox-dsl/` | `ext/snd/` in the tree's `ext/` | `cmdr` would then require libsox and libsoxr to build |
| Write to a temporary file, then rename | Write OUT directly | OUT may be an input, and a failed write would otherwise destroy it |
| One lock around every command | Unrestricted calls | libsox keeps global state, such as its message handler and buffer size |
| OUT's type checked before effects parse their arguments | Checking when the output opens | Some effects' kill functions assume start ran, such as `channels` dereferencing specs only start allocates, so an effect not yet in a chain is freed without kill. Checking first keeps that leak to failures after parsing |
| `snd.segments` prints fixed-point seconds | `cmdr_number_value` | sox's time arguments reject exponents, such as `2.2675736961451248e-05` |
| `_snd` holds the C commands; the module `snd`, `ext/snd/snd.cmdr`, imports it and re-exports them, so `import snd` gives `snd.run` and `snd.out` | `snd-*` for C beside `snd.*` for the module, the first version; then one always-loaded `snd.*` that the module joined | Python's `_socket` under `socket` (README, "Extensions"). The prefixes `_snd.` and `snd.` cannot collide, and the module chooses its exports. Its wrappers `info`, `effects`, and `usage` became re-exports |

## History

The first version linked libsoxr and libsndfile, with nine effects written in C. It was replaced for coverage of sox's features. Its `varispeed` was dropped with it, then restored as a chain effect. Its finding carried over: a ramp keyed to input consumed ran early, since soxr reads input ahead of its output, giving 65365 frames where 61131 were expected. The ramp follows output time.
