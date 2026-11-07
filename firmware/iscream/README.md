# iScream firmware for the FM-1

iScream running on the M-VAVE FM-1 itself, on [fm1-base](https://github.com/charlesvestal/fm1-base) (the
`../fm1-base` submodule) and felucca-bsp. The sound engine in `dsp/` is a C port of MF-1's: the FM voices render
the same samples as MF-1 and the sundaes give the desktop app's voices and effects, which `tests/run_parity.sh`
checks. It has run on one unit (it boots, plays and logged no crash); CPU load and output level are not measured
yet. See `docs/research/fm1-firmware-development.md` for the route and its risks.

| Path | What |
| --- | --- |
| `dsp/` | The engine: msfa FM voices, the six effects, voice allocation, the sundae recipes |
| `dsp/fm_tables.inc`, `dsp/flavors.inc` | Generated from MF-1's sources by `tools/gen_tables.sh`; do not edit |
| `app.c`, `screen.c` | The panel, the saved sundaes and the 240x240 screen |
| `tests/run_parity.sh` | The engine against MF-1's C++ (needs only the Mac's compiler) |
| `tests/basic.txt` | The app in fm1-base's host simulator |
| `tools/render.sh` | A phrase on one flavor through the engine, to a WAV |

## Build and test

Python 3 with Pillow (and on macOS Homebrew's `fribidi`, for Pillow's font shaping) for every build. The device
build also needs Docker, JieLi's toolchain and the AC79 SDK, fetched once from `../fm1-base`:
`sh tools/get_toolchain.sh` and `SDK_URL=https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK sh bsp/tools/get_sdk.sh`.

```sh
tests/run_parity.sh                                   # the engine against MF-1
cd ../fm1-base
python3 tools/build.py ../iscream --host              # the host simulator
../iscream/build/host/iscream_host ../iscream/tests/basic.txt OUT_DIR
JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/.jieli/ac79-sdk \
  python3 tools/build.py ../iscream                   # build/iscream.fwsc
```

## Installing

Back up the FM-1's flash first, and keep a rescue at hand (MvaveFM1Unbricker, or FM-1 Transporter with an RP2040).
Go through [TransitFM1](https://github.com/Quixotic7/TransitFM1) (0.1.4 or later on units with the newer flash
chip): official V15, then TransitFM1, then iScream. TransitFM1 writes the next firmware so that a cut-off install
comes back in update mode. To go back, install M-VAVE's V15 from the same installers.
