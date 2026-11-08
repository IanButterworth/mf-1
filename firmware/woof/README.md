# Woof firmware for the FM-1

Woof running on the M-VAVE FM-1 itself, on [fm1-base](https://github.com/charlesvestal/fm1-base) (the `../fm1-base`
submodule) and felucca-bsp: thirteen dogs, each an FM bark with its own place and mood, and a pixel-art cartoon of
the dog that barks, howls and wags as you play. See `../../docs/woof.md` for the design.

It shares iScream's engine (`../iscream/dsp`: msfa FM voices, the six effects, voice allocation), which
`../iscream/tests/run_parity.sh` checks against MF-1's. What is Woof's own:

| Path | What |
| --- | --- |
| `dsp/wf.c`, `dsp/wf.h` | How a dog's settings become a bark, effects, repeats and the notes a key plays: a C port of `Sources/WoofCore/woof.cc` |
| `dsp/dogs.inc` | The breeds, generated from `Sources/WoofCore/dogs.cc` by `tools/gen_dogs.sh`; do not edit |
| `dsp/scene.c`, `dsp/WoofCore.h` | Copies of the desktop's, made by `tools/gen_dogs.sh` (the device build sees only `firmware/`); do not edit |
| `app.c`, `screen.c` | The panel, the pack, the repeat player (MF-1's arpeggiator, in the audio interrupt), the dog show, the saved dogs and the screen |
| `tests/run_parity.sh` | The port against the desktop's C++: voice bytes, effects, repeats and key notes for every value of every field |
| `tests/basic.txt`, `tests/battery.txt` | The app in fm1-base's host simulator |

It runs on one unit (installed through TransitFM1 on 2026-11-08: it boots, plays and logged no crash). CPU load and
the repeats' timing on the hardware are not measured yet. The scene is 120x64 pixels, so RAM use is about 81 KB of
98 KB.

## Build and test

As for iScream (the `fm1-firmware` skill has the setup):

```sh
tests/run_parity.sh                                   # the port against the desktop app
cd ../fm1-base
python3 tools/build.py ../woof --host                 # the host simulator
../woof/build/host/woof_host ../woof/tests/basic.txt OUT_DIR
JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/.jieli/ac79-sdk \
  python3 tools/build.py ../woof                      # build/woof.fwsc, identity FM-1_4900
```

## Installing

The same route and risks as iScream (`../iscream/README.md`): back up the flash first, keep a rescue at hand, and
go through [TransitFM1](https://github.com/Quixotic7/TransitFM1): official V15, then TransitFM1, then Woof. From
iScream or Almanac, go back to TransitFM1 first.
