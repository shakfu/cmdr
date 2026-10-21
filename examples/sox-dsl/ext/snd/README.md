# snd

sox in-process, through [libsox](https://sourceforge.net/projects/sox/), plus a `varispeed` effect on [libsoxr](https://sourceforge.net/projects/soxr/). They are the module `_snd`; the [snd module](snd.cmdr) builds on them and re-exports them, so `import snd` gives both as `snd.NAME`. Built only into `cmdr-snd`; see [../../README.md](../../README.md).

| Command | Result |
|-|-|
| `snd.run OUT ARG ...` | Run sox with ARGs as the arguments after its output file, writing OUT. Returns OUT. |
| `snd.report OUT ARG ...` | The same, but returns what the effects print, such as `stats` and `stat`. |
| `snd.info PATH` | Lines `type T`, `rate R`, `channels N`, `frames N`, `seconds S`, `bits B`, and `encoding E`, as `soxi`. `frames` and `seconds` are absent if the length is unknown. |
| `snd.segments PATH DB SECONDS` | Lines `START LENGTH`, in seconds, for each part of PATH between silences at or below DB dBFS lasting at least SECONDS. |
| `snd.effects` | The names of the 63 effects: sox's 62, then `varispeed`. |
| `snd.usage EFFECT` | EFFECT's name and arguments, as `sox --help-effect`. |

`sox -m a.wav b.wav -b 16 out.wav reverb gain -3` is:

```text
cmdr> list -b 16 out.wav | snd.run -m a.wav b.wav reverb gain -3
out.wav
```

OUT is a path, or a list of format options and a path. Each ARG is text or a list, whose items are added at any depth. ARGs follow sox's grammar: global options and input files, each after its own format options, then effects. The first word that names an effect starts the effects, and each effect takes the words up to the next effect name, as in sox.

| sox | snd.run |
|-|-|
| Every effect, with sox's arguments | Yes, plus `varispeed`, below |
| `--combine` `sequence`, `concatenate`, `mix`, `mix-power`, `merge`, `multiply`; `-m`, `-M`, `-T` | Yes. Inputs must share a rate; `concatenate` and `sequence` also need equal channels, so `sequence` is `concatenate`. |
| Format options `-v`, `-t`, `-r` (with `k`), `-c`, `-b`, `-e`, and their long forms | Yes; `-v` only on inputs. |
| `-n` null file; `-d` default audio device, from `AUDIODRIVER` and `AUDIODEV` | Yes. `play` is `snd.run -d ...`; `rec` is `snd.run OUT -d trim 0 SECONDS`. |
| `-D`, `--no-dither` | Yes. Otherwise dither is added before output below 24 bits, as sox does. |
| Automatic `rate` and `channels` when OUT's options differ from the chain | Yes |
| `:`, `newfile`, `restart` | No; an error. The module's `split-at`, `split-silence`, and `split-channels` split files. |
| `-` (stdin, stdout), `-p`, `--effects-file`, `--plot`, `-G`, `--norm`, `-V`, `-S`, comments, endianness, `--ignore-length` | No |

`varispeed FROM TO` is not in sox. Speed moves linearly from FROM to TO over the output, as a tape slows or speeds up, so pitch and tempo move together. FROM and TO are each from 1/32 to 32. N input frames give exactly round(2N/(FROM+TO)) output frames, so later effects such as `fade ... -0` see the right length. soxr gives about 0.1% fewer frames, so the end is padded with silence. The input length must be known, as for sox's `fade` with an end position. It runs in libsoxr's variable-rate mode, as in its [examples/5-variable-rate.c](https://github.com/nanake/libsoxr/blob/master/examples/5-variable-rate.c), which sox's `speed` and `bend` do not use.

A regular OUT is written to a temporary file in its directory, then renamed, so a failure leaves OUT unchanged and OUT may also be an input. It gets mode 0666 less the umask, as a new file does; the umask is read once, at the first write. OUT's type is `-t`, else its extension. As in sox, OUT keeps the first input's encoding if its type allows it.

Ctrl-C stops a run, such as a long `-d` playback, and `snd.run` fails with `interrupted`. As for `run`, `try` does not catch it. libsox keeps global state, so the commands hold a lock and run one at a time. `snd.report` reads the effects' output by redirecting fd 2, which is process-wide. While it runs, every thread's stderr goes into its result, not to the terminal.

`snd.segments` exists because sox's `silence ... : newfile : restart` needs chains, which `snd.run` does not run. cmdr values are text without NUL bytes, so audio cannot pass through a pipeline; the commands take paths.
