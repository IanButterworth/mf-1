# Piano for the FM-1

Piano's engine and voicings for `../fm1-modes`: a grand piano made from measurements of a real one, played by its own
engine in place of the FM voices. See `../../docs/piano.md` for the design. Piano's panel, its place as the core's
instrument and the strings on the screen are `../fm1-modes/modes/piano.c`.

| Path | What |
| --- | --- |
| `dsp/piano.c`, `dsp/piano.h`, `dsp/piano_tables.inc` | The engine and its measured tables, the desktop's `Sources/PianoCore` copied unchanged by `tools/gen_piano.sh`; do not edit |
| `dsp/voicings.c`, `dsp/PianoCore.h` | The pianos, their ranges, and what each setting does to the engine and the effects, copied the same way |
| `tests/run_parity.sh` | The copies against the desktop's files; the copy built as C99 without warnings; every voicing playing the same samples unoptimised and optimised |

The engine's state (about 18 KB) lives in the RAM pool. To build and install the firmware, see `../fm1-modes/README.md`.
