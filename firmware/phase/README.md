# Phase's pieces for the FM-1

Phase's sound and processes for `../fm1-modes`: eight pieces, each a cell played by two to four parts that drift,
jump, fill in, follow in canon, pulse, stretch or loop. See `../../docs/phase.md` for the design. Phase's panel, the
phrase the keys write and the graphic score are `../fm1-modes/modes/phase.c`.

| Path | What |
| --- | --- |
| `dsp/ph.c`, `dsp/ph.h` | How a piece's settings become a voice and effects: a C port of `Sources/PhaseCore/phase.cc` |
| `dsp/pieces.inc` | The pieces and sounds, generated from `Sources/PhaseCore/pieces.cc` and `sounds.cc` by `tools/gen_pieces.sh`; do not edit |
| `dsp/process.c`, `dsp/PhaseCore.h` | The desktop's processes, copied unchanged by `tools/gen_pieces.sh` (the device build sees only `firmware/`); do not edit |
| `dsp/player.c`, `dsp/player.h` | The desktop's `player.cc` for the audio interrupt: the processes run on the engine's 64-sample blocks |
| `dsp/div64.c` | The two 64-bit divisions `process.c` needs, as the device links no compiler runtime |
| `tests/run_parity.sh` | The port against the desktop's C++: voice bytes and effects for every value of every field, and the player's notes against the desktop player's, for every piece |

To build and install the firmware, see `../fm1-modes/README.md`.
