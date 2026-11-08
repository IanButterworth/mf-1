---
name: fm1-firmware
description: Build and test the modes firmware for the real FM-1 (firmware/fm1-modes on fm1-base: iScream, Almanac and Woof in one) - the parity tests against MF-1's engine and modes, the host simulator, the device build and its safety checks, regenerating the engine tables, and setting up the toolchain. Use whenever firmware/ code changes, before handing a package to the fm1-device skill, or when a firmware build fails.
---

# Building the FM-1 firmware

The firmware is `firmware/fm1-modes`, an app on fm1-base (`firmware/fm1-base`, a submodule pinned to a tested commit)
and felucca-bsp (fm1-base's submodule), holding iScream, Almanac and Woof behind one shared core (`mode.h` is the
interface). Its engine (`firmware/iscream/dsp`) is a C port of MF-1's; each mode's sound is a C port of the desktop's
in `firmware/iscream/dsp`, `firmware/almanac/dsp` and `firmware/woof/dsp`. Installing on a
device is the fm1-device skill's job, never this one's.

## One-time setup

Check first; set up only what is missing. Downloads go to `~/.jieli` and `~/.fm1-venv`, never into the repo.

1. Submodules: `git submodule update --init --recursive`.
2. Python with Pillow (fonts) and mido + python-rtmidi (the device tools):
   `python3 -m venv ~/.fm1-venv && ~/.fm1-venv/bin/pip install Pillow mido python-rtmidi`.
   On macOS Pillow needs Homebrew's `fribidi` at run time: `brew install fribidi`, and run the build with
   `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib`.
3. Device builds only: Docker (on the Mac, colima with Rosetta:
   `colima start --vm-type vz --vz-rosetta --cpu 4 --memory 6`; it must be running), JieLi's toolchain and the AC79 SDK.
   From `firmware/fm1-base`: `sh tools/get_toolchain.sh` and
   `SDK_URL=https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK sh bsp/tools/get_sdk.sh` (JieLi's GitHub repository is
   gone; the Gitee mirror has the pinned commit, and the script checks the files' hashes). The auto mode
   classifier blocks these downloads as outside code: give Ian the commands to run with `!`.

Set these for every build command below:

```sh
export PY=~/.fm1-venv/bin/python DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib
export JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/.jieli/ac79-sdk
```

## The checks, in order

Run all three after any change under `firmware/`, and the MF-1 suite too when `Sources/` changed.

1. **Parity** (Mac compiler only), one per mode, each must print `all passed`:
   - `firmware/iscream/tests/run_parity.sh`: the 128 factory voices and every flavor bit for bit as MF-1, every
     sundae's voice bytes and effects as the desktop app's, each effect within -60 dB of MF-1's.
   - `firmware/almanac/tests/run_parity.sh` and `firmware/woof/tests/run_parity.sh`: every value of every field
     gives the desktop's voice bytes, effects, arpeggio and key notes.
   Each also fails when its generated tables or copied scene are stale.
2. **Host simulator**: from `firmware/fm1-base`,
   `$PY tools/build.py ../fm1-modes --host`, then
   `../fm1-modes/build/host/modes_host ../fm1-modes/tests/basic.txt OUT_DIR` (OUT_DIR must exist; put it in the
   scratchpad), and `MODES_BATTERY=0` with `tests/battery.txt`. Every line must be `ok`. The script goes through
   every mode by the chooser (a key held across `reboot`). Look at the PNGs it writes (scale them up with
   `sips -z 480 480`).
3. **Device build**: from `firmware/fm1-base`, `$PY tools/build.py ../fm1-modes`. Read every `ok` line it prints:
   - `no 64-bit multiply inlined beside a load` must be there. A line naming functions instead is a real hazard
     (felucca-bsp `docs/CPU.md`: the CPU can corrupt a register); fix it before any install. In the engine, per-sample
     products use exact 32-bit maths, and once-a-block ones go through a `noinline` helper.
   - `no soft-double routines`: no `double` anywhere in the firmware.
   - The image, RAM and pool against their limits. RAM was 83 KB of 98 KB with three modes: the modes share one
     scene buffer (`SCENE_PX_MAX` in `screen.c`), so a new mode's scene must fit it. Keep a margin.
   The package is `firmware/fm1-modes/build/modes.fwsc` (development identity `FM-1_5000`).

## Changing the engine or the flavors

- `dsp/fm_tables.inc` and `dsp/flavors.inc` are generated from MF-1's sources: `firmware/iscream/tools/gen_tables.sh`.
  Never edit them by hand. A change to the flavors in `Sources/IScreamCore` needs a regeneration, then parity.
- The engine must stay bit-identical to MF-1's for the FM voices. If parity fails, the port is wrong, not the test.
- `tools/render.sh FLAVOR OUT.wav` plays a phrase through the firmware's engine, to listen on the Mac.

## Changing a mode

- Almanac: `firmware/almanac/dsp/months.inc`, `scene.c` and `AlmanacCore.h` come from `Sources/AlmanacCore` by
  `firmware/almanac/tools/gen_months.sh`. Woof: `firmware/woof/dsp/dogs.inc`, `scene.c` and `WoofCore.h` come from
  `Sources/WoofCore` by `firmware/woof/tools/gen_dogs.sh`. Rerun them after any change there, never edit by hand.
- A mode's panel and screen are `firmware/fm1-modes/modes/NAME.c`. Its static names share one unit with the core and
  the other modes: give them the mode's prefix.
- A new mode: a C port and parity test of its own, a `modes/NAME.c` filling in an `md_mode_t`, a store object at the
  end of the list in `app.h` and a place at the end of the list in `app.c`, and steps in `tests/basic.txt`.

## Rules the device build enforces, and some it does not

- No C library headers on the device: use `dsp/isc_libc.h`. No `double`, no libm (fm1-base's `fastmath.h`).
- The app shares one compilation unit with the platform: a static name can clash with felucca-bsp's (`rng` did).
- The audio interrupt owns the engine. The screen loop hands it changes under `plat_lock`, never by calling it.
- Flash writes silence the sound for a moment: the app saves only when nothing plays.
