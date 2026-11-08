# Almanac's months for the FM-1

Almanac's sound for `../fm1-modes`: twelve months as twelve FM voices, each with its weather, place and mood. See
`../../docs/almanac.md` for the design. Almanac's panel and screen are `../fm1-modes/modes/almanac.c`.

| Path | What |
| --- | --- |
| `dsp/alm.c`, `dsp/alm.h` | How a day becomes a voice, effects, a pattern and the notes a key plays: a C port of `Sources/AlmanacCore/almanac.cc` |
| `dsp/months.inc` | The months and skies, generated from `Sources/AlmanacCore/months.cc` by `tools/gen_months.sh`; do not edit |
| `dsp/scene.c`, `dsp/AlmanacCore.h` | Copies of the desktop's, made by `tools/gen_months.sh` (the device build sees only `firmware/`); do not edit |
| `tests/run_parity.sh` | The port against the desktop's C++: voice bytes, effects, patterns and key notes for every value of every field |

To build and install the firmware, see `../fm1-modes/README.md`.
