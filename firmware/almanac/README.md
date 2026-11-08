# Almanac firmware for the FM-1

Almanac running on the M-VAVE FM-1 itself, on [fm1-base](https://github.com/charlesvestal/fm1-base) (the
`../fm1-base` submodule) and felucca-bsp: twelve months as twelve FM voices, each with its weather, place and mood,
and a pixel-art landscape that rains, snows and blows as you play. See `../../docs/almanac.md` for the design.

It shares iScream's engine (`../iscream/dsp`: msfa FM voices, the six effects, voice allocation), which
`../iscream/tests/run_parity.sh` checks against MF-1's. What is Almanac's own:

| Path | What |
| --- | --- |
| `dsp/alm.c`, `dsp/alm.h` | How a day becomes a voice, effects, a pattern and the notes a key plays: a C port of `Sources/AlmanacCore/almanac.cc` |
| `dsp/months.inc` | The months and skies, generated from `Sources/AlmanacCore/months.cc` by `tools/gen_months.sh`; do not edit |
| `dsp/scene.c`, `dsp/AlmanacCore.h` | Copies of the desktop's, made by `tools/gen_months.sh` (the device build sees only `firmware/`); do not edit |
| `app.c`, `screen.c` | The panel, chords and splits, the pattern player (MF-1's arpeggiator, in the audio interrupt), the saved days and the screen |
| `tests/run_parity.sh` | The port against the desktop's C++: voice bytes, effects, patterns and key notes for every value of every field |
| `tests/basic.txt`, `tests/battery.txt` | The app in fm1-base's host simulator |

It runs on one unit (installed through TransitFM1 on 2026-11-08: it boots, plays and logged no crash). CPU load and
the pattern's timing on the hardware are not measured yet.

## Build and test

As for iScream (the `fm1-firmware` skill has the setup):

```sh
tests/run_parity.sh                                   # the port against the desktop app
cd ../fm1-base
python3 tools/build.py ../almanac --host              # the host simulator
../almanac/build/host/almanac_host ../almanac/tests/basic.txt OUT_DIR
JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/.jieli/ac79-sdk \
  python3 tools/build.py ../almanac                   # build/almanac.fwsc, identity FM-1_4800
```

## Installing

The same route and risks as iScream (`../iscream/README.md`): back up the flash first, keep a rescue at hand, and
go through [TransitFM1](https://github.com/Quixotic7/TransitFM1): official V15, then TransitFM1, then Almanac. From
iScream, go back to TransitFM1 first.
