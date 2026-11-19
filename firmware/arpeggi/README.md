# Arpeggi's scenes for the FM-1

Arpeggi's sound and lanes for `../fm1-modes`: eight scenes, each two arpeggiators playing one pool of notes in a
relation of their own. See `../../docs/arpeggi.md` for the design. Arpeggi's panel, the keys that feed the pool and
the night sky on the screen are `../fm1-modes/modes/arpeggi.c`.

| Path | What |
| --- | --- |
| `dsp/ap.c`, `dsp/ap.h` | How a scene's settings become a voice and effects: a C port of `Sources/ArpeggiCore/arpeggi.cc` |
| `dsp/scenes.inc` | The scenes and sounds, generated from `Sources/ArpeggiCore/scenes.cc` and `sounds.cc` by `tools/gen_scenes.sh`; do not edit |
| `dsp/arp.c`, `dsp/ArpeggiCore.h` | The desktop's pool and lanes, copied unchanged by `tools/gen_scenes.sh` (the device build sees only `firmware/`); do not edit |
| `dsp/ap_player.c`, `dsp/ap_player.h` | The desktop's `player.cc` for the audio interrupt: the lanes run on the engine's 64-sample blocks |
| `tests/run_parity.sh` | The port against the desktop's C++: voice bytes and effects for every value of every field, and the player's notes against the desktop player's, through keys, changes and stops |

The 64-bit divisions `arp.c` needs come from `../phase/dsp/div64.c`, which the firmware links once for both modes.
To build and install the firmware, see `../fm1-modes/README.md`.
