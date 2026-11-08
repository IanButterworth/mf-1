# MF-1's modes on the FM-1

One firmware for the M-VAVE FM-1 that runs MF-1's modes: iScream, Almanac and Woof. It is an app on
[fm1-base](https://github.com/charlesvestal/fm1-base) (the `../fm1-base` submodule) and felucca-bsp.

- **Choosing a mode:** switch on normally and the last mode used starts (iScream the first time). Hold any piano key
  while switching on for the chooser, let go, then press the key under a mode's name: F for iScream, G for Almanac,
  A for Woof.
- **Settings:** each mode keeps its own, in a store object of its own, so switching loses nothing.
- **Battery:** the bars show the FM-1's own reading, with LOW BATTERY over the screen at the last bar.

It has run on one unit (installed through TransitFM1 on 2026-11-08, identity `FM-1_5000`): it boots, plays in every
mode and logged no crash. CPU load, output level and timing on the hardware are not measured yet. See
`../../docs/research/fm1-firmware-development.md` for the route and its risks.

## How it is built

A shared core owns everything the modes have in common; each mode is one small file behind the interface in
`mode.h`. The modes' sound comes from their C ports of the desktop app, which stay beside their parity tests.

| Path | What |
| --- | --- |
| `app.c` | The core: the engine hand-off, keys, the arpeggiator, MIDI, the panel, saving, the chooser |
| `screen.c` | The shared screen: bands, drawing helpers, scene blit, tabs, value chips, pop-up, chooser |
| `mode.h` | What a mode gives the core: its rows of settings, voice, effects, arpeggio, key notes, pages, screen |
| `modes/iscream.c`, `modes/almanac.c`, `modes/woof.c` | Each mode's panel and screen |
| `../iscream/dsp` | The engine (msfa FM voices, the six effects, voice allocation) and iScream's sundae recipes |
| `../almanac/dsp`, `../woof/dsp` | Almanac's and Woof's C ports and their scenes (copies of the desktop's `scene.c`) |
| `tests/basic.txt`, `tests/battery.txt` | The firmware in fm1-base's host simulator, through every mode and the chooser |

All the modes share one scene buffer and one engine, so a mode costs only its own code and tables: the three need a
211 KB image and 83 KB of RAM (of 98 KB).

To add a mode: give it a C port and parity test of its own beside the others, a `modes/NAME.c` that fills in an
`md_mode_t`, a store object at the end of the list in `app.h`, and a place at the end of the list in `app.c`.

## Build and test

Python 3 with Pillow (and on macOS Homebrew's `fribidi`, for Pillow's font shaping) for every build. The device
build also needs Docker, JieLi's toolchain and the AC79 SDK, fetched once from `../fm1-base`:
`sh tools/get_toolchain.sh` and `SDK_URL=https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK sh bsp/tools/get_sdk.sh`.
The `fm1-firmware` skill has the details.

```sh
../iscream/tests/run_parity.sh                        # each mode's port against the desktop app
../almanac/tests/run_parity.sh
../woof/tests/run_parity.sh
cd ../fm1-base
python3 tools/build.py ../fm1-modes --host            # the host simulator
../fm1-modes/build/host/modes_host ../fm1-modes/tests/basic.txt OUT_DIR
JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/.jieli/ac79-sdk \
  python3 tools/build.py ../fm1-modes                 # build/modes.fwsc, identity FM-1_5000
```

## Installing

Back up the FM-1's flash first, and keep a rescue at hand (MvaveFM1Unbricker, or FM-1 Transporter with an RP2040).
Go through [TransitFM1](https://github.com/Quixotic7/TransitFM1) (0.1.4 or later on units with the newer flash
chip): official V15, then TransitFM1, then this firmware. TransitFM1 writes the next firmware so that a cut-off
install comes back in update mode. While the firmware is young, go back through TransitFM1 for each update too. To
go back to stock, install M-VAVE's V15 from the same installers. You do this at your own risk.
