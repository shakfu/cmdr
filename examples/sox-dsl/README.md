# Sox DSL

sox in cmdr. The [snd](ext/snd/README.md) extension runs sox's effects chains in-process through libsox, and adds a `varispeed` effect on libsoxr. The extension commands, `snd-NAME`, are the low-level layer, as `_socket` is to Python's `socket`. The [snd](snd/__init__.cmdr) module definitions, `snd.NAME` after `import snd`, are the API scripts use. They build a chain as a list of sox's arguments.

| File | Contents |
|-|-|
| [ext/snd/](ext/snd/README.md) | `snd-run`, `snd-report`, `snd-info`, `snd-segments`, `snd-effects`, `snd-usage` |
| [snd/](snd/__init__.cmdr) | The `snd` module: inputs, combining, effects, output, inspection, splitting, presets |
| [demo.cmdr](demo.cmdr) | Synthesizes tones, then combines, processes, and splits them |
| [tests/](tests) | Checks every `snd` module definition |
| [CMakeLists.txt](CMakeLists.txt) | Builds `cmdr-snd` |

```sh
brew install sox libsoxr    # or the distribution's libsox-dev and libsoxr-dev
make
./build/examples/sox-dsl/cmdr-snd examples/sox-dsl/demo.cmdr
```

The build makes `build/examples/sox-dsl/cmdr-snd`: `cmdr` with `ext/` and `ext/snd`. pkg-config must find `sox` and `soxr`, or configure skips this example with a status message. `cmdr` itself links neither. The demo writes into `SND_OUT`, default `snd-out/`. CTest runs the demo, `tests/test_snd.cmdr`, and `ext/snd/tests/test.cmdr`. None of them use an audio device.

## Chains

A chain is a list: options and input files, then effects, as sox takes them after its output file. Definitions that build a chain only add words; `snd.out`, `snd.play`, `snd.stats`, and the splits run it.

```text
cmdr> import snd
cmdr> snd.in a.wav | snd.rate 48000 | snd.effect "reverb 50 50 100" | snd.fade 0.1 0.5 | unwords
a.wav rate 48000 reverb 50 50 100 fade 0.1 -0 0.5
cmdr> list a.wav b.wav | snd.mix | snd.norm -1 | with { (c) snd.out $c chord.wav }
chord.wav
cmdr> snd.in chord.wav | snd.stats-field "Pk lev dB"
-1.00
```

| Kind | Definitions |
|-|-|
| Inputs | `in PATH`, `in-as OPTIONS PATH`, `null`, `device`, `tone SECONDS HZ`, `scaled PATH FACTOR` |
| Combining | `combine METHOD PATHS`, `join`, `mix`, `mix-power`, `merge`, `multiply` |
| Any effect | `effect CHAIN SPEC`, as `snd.effect "chorus 0.7 0.9 55 0.4 0.25 2 -t"` |
| Named effects | `rate`, `speed`, `tempo`, `varispeed`, `pitch`, `gain`, `norm`, `reverse`, `pad`, `repeat`, `fade`, `trim`, `highpass`, `lowpass`, `reverb` |
| Channels | `channels N`, `mono`, `stereo`, `remix SPEC`, `swap`. `merge` joins files' channels; `split-channels` separates them. |
| Output | `out CHAIN PATH`, `out-as CHAIN OPTIONS PATH`, `play CHAIN`, `record SECONDS PATH` |
| Inspecting | `info PATH`, `info-field PATH NAME`, `stats CHAIN`, `stats-field CHAIN NAME`, `effects`, `usage EFFECT` |
| Splitting | `split-at CHAIN POINTS PATH`, `split-silence CHAIN DB SECONDS PATH`, `split-channels CHAIN PATH` |
| Presets | `telephone`, `tape-stop` |

`help` gives each definition's arguments. SPEC, OPTIONS, and POINTS are text, split into words. The named effects have fixed arguments, since a `def` has fixed parameters; `effect` takes any of the 63 effects, with sox's own arguments. `varispeed FROM TO`, not in sox, changes speed linearly over the output, as a tape does; `tape-stop` is `varispeed 1 0.1` with a short fade. `fade IN OUT` is sox's `fade IN -0 OUT`. Like sox, `fade` with an end position fails after an effect that makes the length unknown, such as `chorus`, so put it first.

`combine` and its forms take a list of paths. `scaled` makes an item with sox's `-v`; as in sox, `mix` scales each input by 1/N only if no input is scaled.

```text
cmdr> snd.scaled b.wav 0.3 | with { (b) list a.wav $b } | snd.mix | with { (c) snd.out $c duck.wav }
```

Each split runs the chain once into `PATH` numbered 0, cuts that file, deletes it, and returns the paths written. Parts are numbered from 1 before the extension, as sox's `newfile` names them: `take.wav` gives `take001.wav`, `take002.wav`.

```text
cmdr> snd.in talk.wav | snd.split-silence -45 0.5 phrase.wav
phrase001.wav
phrase002.wav
```

A preset is a `def` from chain to chain:

```text
cmdr> def lofi { (c) snd.rate $c 11025 | snd.mono | snd.effect "overdrive 10" | snd.norm -1 }
cmdr> files in | keep { ends-with .wav } | each { (f) snd.in $f | lofi | with { (c) path-basename $f | with { (b) path-join out $b } | with { (o) snd.out $c $o } } }
```

`snd.play CHAIN` plays on the default audio device and Ctrl-C stops it. `snd.record 5 take.wav` records five seconds. What `snd-run` covers of sox, and what it does not, is in [ext/snd](ext/snd/README.md).

Design decisions: [docs/dev/design.md](docs/dev/design.md).
