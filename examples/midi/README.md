# MIDI

MIDI generation in cmdr. The [midi](ext/midi/README.md) extension plays scores on MIDI ports, reads and writes Standard MIDI Files through the vendored [libremidi](../../thirdparty/libremidi-5.4.3) 5.4.3, and makes Bjorklund's Euclidean rhythms. The [midi](ext/midi/midi.cmdr) module builds, changes, and randomizes scores. `import midi` gives both the C commands from `_midi`, such as `midi.read`, and the module's definitions, such as `midi.seq`. The module is built into `cmdr` with `-DCMDR_MIDI=ON`, so it needs no `CMDR_PATH`. Its chance definitions draw from [ext/random](../../ext/random/README.md), so `random.seed N` repeats a piece.

| File | Contents |
|-|-|
| [ext/midi/](ext/midi/README.md) | `midi.key`, `midi.bjorklund`, `midi.ports`, `midi.play`, `midi.write`, `midi.read` |
| [ext/midi/midi.cmdr](ext/midi/midi.cmdr) | The `midi` module: items, scales and chords, rhythm, chance, transforms, output |
| [demo.cmdr](demo.cmdr) | Builds bass, pads, drums, and a random melody, and writes them as MIDI files |
| [tests/](tests) | Checks every `midi` module definition |
| [extension.cmake](extension.cmake) | Adds libremidi for `-DCMDR_MIDI` |
| [CMakeLists.txt](CMakeLists.txt) | The tests |

```sh
make CMAKE_FLAGS=-DCMDR_MIDI=ON
./build/cmdr examples/midi/demo.cmdr
```

`-DCMDR_MIDI=ON` builds `ext/midi` into `cmdr`. It needs a C++20 compiler and CMake 3.22, or configure fails; `-DCMDR_MIDI=AUTO` skips it instead. libremidi is built with only the system MIDI API: CoreMIDI on macOS, ALSA on Linux. The demo writes into `MIDI_OUT`, default `midi-out/`. With midi built in, CTest runs the demo, `tests/test_midi.cmdr`, and `ext/midi/tests/`. Only `test_midi.c` uses the MIDI service, through a virtual port of its own; it is skipped where none runs.

## Scores

A score is a list of items. Each is text: its kind, its START in beats (quarter notes), then the kind's fields.

| Item | Fields |
|-|-|
| `note START LENGTH KEY VELOCITY CHANNEL` | KEY is 0 to 127 or a name such as `C4` (60) or `F#3`; VELOCITY 1 to 127 |
| `rest START LENGTH` | Silence that counts toward the score's length |
| `cc START CONTROLLER VALUE CHANNEL` | Both 0 to 127 |
| `program START PROGRAM CHANNEL` | PROGRAM 0 to 127; General MIDI's piano is 0 |
| `bend START VALUE CHANNEL` | VALUE -8192 to 8191; 0 is centre |
| `tempo START BPM` | From START on |

CHANNEL is 1 to 16. Scores print one item per line, so `write` and `read` store them as text.

```text
cmdr> import midi
cmdr> midi.seq "C4 E4 . C5" 0.5
note 0 0.5 60 100 1
note 0.5 0.5 64 100 1
rest 1 0.5
note 1.5 0.5 72 100 1
cmdr> midi.scale A2 minor | midi.degrees "1 4 5" | midi.seq 2 | midi.transpose 12 | midi.write bass.mid 96
bass.mid
cmdr> random.seed 7
cmdr> midi.scale A4 minor-pentatonic 2 | midi.walk 16 | midi.pick 16 0.25 | midi.chance 0.8 | midi.play 120 "IAC Driver Bus 1"
```

| Kind | Definitions |
|-|-|
| Items | `note KEY [LENGTH VELOCITY CHANNEL]`, `rest LENGTH`, `cc CONTROLLER VALUE [CHANNEL]`, `program NUMBER [CHANNEL]`, `bend VALUE [CHANNEL]`, `tempo BPM`, `ramp CONTROLLER FROM TO LENGTH [STEPS CHANNEL]`. Each starts at beat 0. |
| Sequences | `seq KEYS [LENGTH VELOCITY CHANNEL]` plays KEYS in turn, with `.` as a rest; `chord KEYS [...]` plays them together. KEYS is a list or text of words. Defaults: 1 beat, velocity 100, channel 1. |
| Keys | `scale ROOT [KIND OCTAVES]`, `chord-keys ROOT [KIND]`, `degrees SCALE DS`. `scales` and `chords` list the KINDs. |
| Rhythm | `euclid K N KEY [ROTATE]`: N steps, KEY on K of them as Bjorklund's algorithm spreads them, `.` elsewhere, rotated left ROTATE steps; for `seq`. `rotate LIST N`. |
| Chance | `pick KEYS N [...]`: N keys drawn from KEYS. `walk SCALE N [START STEP]`: N keys by a random walk over SCALE. `chance SCORE P`: each note kept with probability P, else a rest. `humanize SCORE [TIME VELOCITY]`: random offsets to starts and velocities. |
| Changing | `transpose`, `shift BEATS`, `stretch FACTOR`, `velocity`, `channel`, `end` |
| Combining | `stack A B` plays together; `then A B` plays B after A; `sequence SCORES` plays a list in order; `loop SCORE N` |
| Fields | `field ITEM I`, `put ITEM I V`, `edit SCORE KIND I BLOCK` on field I of items of KIND; field 1 is the kind, 2 the start |
| Output | `play SCORE [BPM PORT]`, `write SCORE PATH [BPM]`, `read PATH`, `ports`: the C commands, with BPM 120 by default |

`help midi.NAME` gives a definition's arguments and defaults.

A score ends where its last note or rest ends, so `then`, `sequence`, and `loop` keep trailing rests, and files keep them as the track's end. Changes apply to the kinds with the field: `transpose` to notes, `channel` to notes, controllers, programs, and bends, `stretch` to all starts and to note and rest lengths. Place a part with `shift`, `then`, or `sequence`: `midi.program 33 2 | midi.stack $bass` sets the bass's instrument at beat 0.

The chance definitions draw from ext/random, whose draws match Python's `random` for the same seed. `humanize` can move the last note and so the end, so apply it after `loop` and `then`. `pick` and `walk` give keys; `chance` and `humanize` change scores. For several independent draws, call a definition with `repeat`, which needs no counter:

```text
cmdr> repeat 4 { () midi.scale C4 | midi.walk 4 } | each { (ks) midi.seq $ks 0.25 } | midi.sequence
```

`play` blocks until the score ends, trailing rests included, and Ctrl-C stops it, releasing any notes still sounding. With no PORT it uses the first output port that `ports` lists. macOS has no output port until a synthesizer or DAW makes one, or the IAC Driver is enabled in Audio MIDI Setup. A score's `tempo` at beat 0 overrides BPM.

`from midi import rest` collides with the prelude's `rest`, so bind it with `as`: `from midi import rest as pause`.
