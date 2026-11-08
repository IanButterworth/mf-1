# Woof's dogs for the FM-1

Woof's sound for `../fm1-modes`: thirteen dogs, each an FM bark with its own place and mood. See
`../../docs/woof.md` for the design. Woof's panel, screen and dog show are `../fm1-modes/modes/woof.c`.

| Path | What |
| --- | --- |
| `dsp/wf.c`, `dsp/wf.h` | How a dog's settings become a bark, effects, repeats and the notes a key plays: a C port of `Sources/WoofCore/woof.cc` |
| `dsp/dogs.inc` | The breeds, generated from `Sources/WoofCore/dogs.cc` by `tools/gen_dogs.sh`; do not edit |
| `dsp/scene.c`, `dsp/WoofCore.h` | Copies of the desktop's, made by `tools/gen_dogs.sh` (the device build sees only `firmware/`); do not edit |
| `tests/run_parity.sh` | The port against the desktop's C++: voice bytes, effects, repeats and key notes for every value of every field |

To build and install the firmware, see `../fm1-modes/README.md`.
