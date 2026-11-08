# iScream's engine and recipes for the FM-1

The sound engine every mode of `../fm1-modes` runs on, and iScream's sundae recipes. `dsp/` is a C port of MF-1's:
the FM voices render the same samples as MF-1, and the sundaes give the desktop app's voices and effects, which
`tests/run_parity.sh` checks. iScream's panel and screen are `../fm1-modes/modes/iscream.c`.

| Path | What |
| --- | --- |
| `dsp/` | The engine: msfa FM voices, the six effects, voice allocation, the sundae recipes |
| `dsp/fm_tables.inc`, `dsp/flavors.inc` | Generated from MF-1's sources by `tools/gen_tables.sh`; do not edit |
| `tests/run_parity.sh` | The engine and the sundaes against MF-1's C++ (needs only the Mac's compiler) |
| `tools/render.sh` | A phrase on one flavor through the engine, to a WAV |

To build and install the firmware, see `../fm1-modes/README.md`.
