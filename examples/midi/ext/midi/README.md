# midi

MIDI ports and Standard MIDI Files, through [libremidi](../../../../thirdparty/libremidi-5.4.3), and Euclidean rhythms. They are the module `_midi`; the [midi module](midi.cmdr) builds on them and re-exports them, so `import midi` gives both as `midi.NAME`. Built into `cmdr` only with `-DCMDR_MIDI=ON`; see [../../README.md](../../README.md).

| Command | Result |
|-|-|
| `midi.key KEY` | KEY as 0 to 127. KEY is a number, or a letter, any number of `#` or `b`, and an octave from -1 to 9, with `C4` as 60 and `C-1` as 0. |
| `midi.bjorklund K N` | A list of N steps, `1` on K of them and `0` elsewhere, by Bjorklund's algorithm. N is 1 to 4096. |
| `midi.ports` | The names of the MIDI output ports, hardware and virtual. |
| `midi.play SCORE [BPM] [PORT]` | Play SCORE starting at BPM, default 120, on the output port named PORT, or the first one if PORT is absent or empty. Returns SCORE. |
| `midi.write SCORE PATH [BPM]` | Write SCORE to PATH as a format 0 MIDI file at 480 ticks per beat, with BPM 120 by default. Returns PATH. |
| `midi.read PATH` | The items of every track of the MIDI file PATH, in beats, sorted by start. |

A score is a list of items, or text with one item per line. The items are `note`, `rest`, `cc`, `program`, `bend`, and `tempo`, as the [module README](../../README.md#scores) lists them. An invalid item fails the command with its position, as `midi.write: item 2: CHANNEL 17 is not 1 to 16`.

At equal times, items are sent in this order: tempo, program, cc, bend, note-off, note-on. So a channel is set up before its notes, and a key struck again as it ends sounds again. A note of length 0 sends its note-on first. A score ends where its last note or rest ends.

`midi.play` follows `tempo` items, and BPM sets the tempo until the first. It returns after the score's end, so a trailing rest delays the next command. Ctrl-C stops it: it sends a note-off for each note still sounding, then fails with `interrupted`.

`midi.write` writes BPM as the tempo at tick 0, unless SCORE has a `tempo` at beat 0. It rounds times to 1/480 beat and ends the track at the score's end. It writes to a temporary file beside PATH, then renames it, so a failure leaves PATH unchanged. The file gets mode 0666 less the umask, as a new file does; the umask is read once, at the first write. BPM, here and in `tempo` items, is from 3.6 to 60,000,000, the range of a file's tempo field.

`midi.read` returns notes, controllers, programs, bends, and tempos. It drops other events, such as aftertouch, SysEx, and text. A note-on pairs with the next note-off, or note-on of velocity 0, of its channel and key, first in, first out. A note never released ends at the file's end. If the file ends after its last item, a `rest` fills the gap; gaps within a score are implied by starts. A tempo prints with the fewest digits that give the file's value, as `96.5` rather than `96.49996`. Files timed in SMPTE frames, and truncated files, are errors.

`midi.bjorklund` follows Toussaint's description of Bjorklund's algorithm, in [The Euclidean Algorithm Generates Traditional Musical Rhythms](https://cgm.cs.mcgill.ca/~godfried/publications/banff.pdf) (2005). Start with K groups `1` and N-K groups `0`. Append the second set's groups to the first set's, pairwise, and keep the unpaired groups as the second set. Do this once, then again while the second set has more than one group. The tests check the 11 patterns that paper lists, from E(2,5) to E(13,24).
