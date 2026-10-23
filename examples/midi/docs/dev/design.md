# MIDI design

Decision record, 2026-10-20.

| Choice | Over | Because |
|-|-|-|
| An item is text, `KIND START ...`, in beats | Handles to sequences held in C, or MIDI bytes | Scores print, diff, and pass through `write`, `read`, `keep`, and `filter`. cmdr has no finalizers, so handles would leak. Beats keep tempo out of every item but `tempo` |
| The kind is the first word of every item | Untagged notes, with other kinds tagged | One rule to parse, and `matches "^note "` selects a kind. A score of notes costs a word per item |
| `rest` items | Gaps implied by starts only | A trailing gap has no item to start after it, so `then`, `loop`, and files lost it. Rests within a score are kept for `then` and `end` but not written: a file has no rest event, so `midi.read` restores only the trailing one, as the track's end |
| `tempo` items, with BPM as the tempo before the first | Tempo only as a `play` or `save` argument | Tempo changes are part of a piece. BPM stays so a score of notes plays at any tempo |
| Notes, rests, controllers, programs, bends, and tempo | All channel and meta events | These are what generation needs. Aftertouch, SysEx, and text would be a kind each, with no generator using them yet |
| Generation in the module | Generators in C | `seq`, `scale`, `walk`, `chance`, and the transforms are list operations cmdr already has. C is only where libremidi is, and for `midi.bjorklund` |
| `midi.bjorklund` in C | Bjorklund's algorithm in the module | It regroups until one group remains. A `def` cannot call itself, and a bounded `fold` that does nothing after the last regroup is harder to read than the C |
| Bjorklund's algorithm, as Toussaint describes it | `(I * K) mod N < K`, the first version | The modular form gives the same spacing up to rotation, but not the published patterns: 5 of 8 was `x.x.xx.x`, not the cinquillo `x.xx.xx.`. The recursive count-and-remainder form in common use also rotates: it gives E(3,4) as `xxx.`, the paper `x.xx` |
| Randomness in `ext/random`, built into `cmdr` | Random commands in `ext/midi` | Random draws are not MIDI-specific; cmdr had none |
| Python's Mersenne Twister and seeding | A smaller generator, such as xorshift | A seed means what it means in Python, so tests and pieces can be checked against Python |
| Generators draw, test with a predicate, then switch to the item with a lone `$n` stage | A block that ignores its argument | cmdr rejects unused parameters, and `or`'s fallback is evaluated before it is chosen |
| libremidi's C API for ports, its `reader` and `writer` in `smf.cpp` | libremidi's C++ API throughout | The extension stays C like the rest of cmdr; the file classes have no C API. `smf.cpp` only encodes and decodes bytes, so file access and errors stay in `midi.c` |
| `smf.cpp` moves the end-of-track event to the score's end | Ending at the last event, as libremidi's writer does | That loses a trailing rest. The writer drops end-of-track events it is given, so `smf.cpp` rewrites the last delta and the track length |
| `add_subdirectory` of the vendored source, `EXCLUDE_FROM_ALL` | `find_package(libremidi)` | libremidi is not packaged on most systems. `EXCLUDE_FROM_ALL` keeps its headers out of `cmake --install` |
| The vendored copy omits `examples/`, `book/`, `src/`, `include/libremidi/client.*`, and `include/libremidi/protocols/` | The full release | They are used only by libremidi's examples, tests, docs, and C++ module build, which this build does not enable; 2.1 MB becomes 1.3 MB, and no libremidi file is edited. Unused backends stay: `configurations.hpp` includes every backend's `config.hpp`, and the source list names each backend's headers, so trimming them means patching libremidi |
| Boost, network, JACK, PipeWire, and keyboard backends off | libremidi's defaults | PipeWire's backend fetches readerwriterqueue from the network at configure time. The rest are unused |
| `-DCMDR_MIDI`, default `OFF`, builds `ext/midi` into `cmdr` | A separate binary, `cmdr-midi`, until 2026-10-23; or `ext/midi/` in the tree's `ext/` | One binary takes any set of extensions. In `ext/`, `cmdr` would always need a C++20 compiler and a MIDI service library |
| `_midi` holds the C commands; the module `midi`, `ext/midi/midi.cmdr`, re-exports them, so `import midi` gives `midi.read` and `midi.seq` | `midi-*` for C beside `midi.*` for the module, the first version | Python's `_socket` under `socket` (README, "Extensions"). A name means one thing: the C command `euclid K N` became `bjorklund`, since the module's `euclid K N KEY` differs; `play` and `write` took BPM 120 by default in C, replacing module wrappers |
| Validate items in C | Clamp, as libremidi's `channel_events` do | A channel of 17 is a bug in the score; clamping plays it on channel 16 |
| `test_midi.c` plays into a virtual input port of its own | Skipping `midi.play` in CTest | It checks the bytes sent, their order, the tempo change's timing, and the release on interrupt without hardware. It exits 77, skipped, where the port cannot open |
