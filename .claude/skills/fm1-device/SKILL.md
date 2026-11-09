---
name: fm1-device
description: Work safely with a real M-VAVE FM-1 over USB - identify what it runs, back up its whole flash, install the modes firmware or any firmware package, check it after an install, recover a unit that will not start, and go back to M-VAVE's stock firmware. Use for anything that reads from or writes to a connected FM-1, including "flash it", "install", "try it on the device", "it won't boot" and "go back to stock".
---

# The FM-1 on the desk

A write to the FM-1 can leave it dark. The flash has a single bank, there is no debug header, and the community has
bricked units with exactly these tools. Everything below is about never being one step away from a dead unit.

## Rules

1. **Ask before every write.** An install, a restore, a rescue with `--write`, a TransitFM1 hop: each needs Ian's
   explicit yes in chat for that action, after you have shown him what will happen (current firmware and identity,
   the package's file name, identity and SHA-256, the route). A yes for one install is not a yes for the next.
   Reading (`--info`, `fm1_remote.py screen`, `log`, `crashes`) needs no confirmation.
2. **Backup before the first write to a unit**, and keep it: a full 1 MiB flash image (below), saved outside the
   repo under `~/Documents/GitHub/mf-1-ref/fm1-backups/` with the date and what the unit ran. Never commit a
   backup or any M-VAVE image: they hold M-VAVE's firmware and the owner's data.
3. **The right port.** fm1-base's tools pick a MIDI port whose name contains "FM-1". MF-1's own virtual port is
   "MF-1 (FM-1 Compatible)", and its modes' ports "iScream (FM-1 Compatible)", "Almanac (FM-1 Compatible)", "Woof (FM-1 Compatible)" and "Phase (FM-1 Compatible)": quit MF-1 before any device
   tool, then confirm with `--info` that the answer is the hardware. Pass `--port` when in doubt. No DAW or editor
   may send MIDI to the FM-1 during an install.
4. **Power.** The FM-1 on USB, battery charged, the cable not touched until the tool reports the new identity.
5. **One tool at a time**, and never a second power cycle in a row while the FM-1 is in update mode: the second
   one drops it into the chip's ROM boot (`WL82 UBOOT1.00`), which needs the rescue route.

## Tools

From `firmware/fm1-base`, with `PY=~/.fm1-venv/bin/python` (set up by the fm1-firmware skill):

| Command | What |
| --- | --- |
| `$PY tools/fm1_install.py --info` | What runs: identity (`FM-1_015` is stock V15 or TransitFM1, `FM-1_50..` the modes firmware), read only |
| `$PY tools/fm1_install.py PACKAGE.fwsc` | Install; asks on the terminal unless `--yes`; resumes a cut-off install |
| `$PY tools/fm1_remote.py screen` / `log` / `crashes --elf ../fm1-modes/build/modes.elf` | Read a running fm1-base firmware: its screen in words, the last crash with registers, the crash history with symbols |
| `bsp/tools/fm1_rescue.py` (sudo, pyusb) | ROM boot: checks the chip and flash, backs up the whole flash, writes only with `--write` |

Exit codes of `fm1_install.py`: 0 done, 3 not found, 4 connection lost, 5 timeout, 6 wrong model or identity. On
4 or 5, stop and follow "If an install is cut off", never retry blind.

## Before the first install on a unit

1. Quit MF-1, connect the FM-1, run `--info`. Note the identity and firmware version.
2. Find the flash chip: the newer units (logo on the front) have the UC25HQ80 (`0xB36014`); older ones the Puya
   (`0x856014`). The full-flash backup below prints it. On the UC25HQ80, TransitFM1 must be 0.1.4 or later and
   a firmware that knows only the Puya keeps its saves in RAM and refuses later installs.
3. Take the full-flash backup, twice, and check the two reads are identical. It needs the chip's ROM boot:
   - fm1_rescue run read only (no `--write`). It needs pyusb and libusb-package in `~/.fm1-venv`, and both stock
     packages, checked against the hashes in the script: V15 from FM1 Open (below), V16 from M-VAVE's download URL.
     Pass `--loader` with a copy of `wl82loader.bin` you have checked against the script's hash. It saves the
     image next to the first package.
   - sudo needs a terminal for its password, so Ian runs it in his own Terminal window, not with `!`.
   - MvaveFM1Unbricker's double read (github.com/Quixotic7/MvaveFM1Unbricker) works on Windows and Linux only.

   From stock V15, holding OCT- and OCT+ at power-on did not reach ROM boot (2026-11-07). The Unbricker's SysEx key
   did. Ian starts fm1_rescue first (it waits 120 s), then runs `$PY .claude/skills/fm1-device/fm1_softkey.py`
   from the repo root. The script checks for a stock identity on the hardware port before it sends the key.
   Expect a black screen, then `WL82 UBOOT1.00`. One power cycle afterwards brings V15 back.
   Never send the key from a one-liner: CoreMIDI drops a message sent just after a port opens.
4. A factory V15 unit is not the package's V15 build (`docs/research/fm1-firmware-development.md`), so nearly
   all app sectors differ from the package; that is expected. The backup is the only copy of the factory build.
5. Make sure a rescue path exists before going on: the Unbricker (plain USB, needs the unit to reach ROM boot)
   or the FM-1 Transporter (a Seeed XIAO RP2040 on the USB data lines, for a unit silent on USB).

## The install route

V16 -> V15 -> TransitFM1 -> the modes firmware, each arrow one confirmed write.

- **From V16**: back to V15 with M-VAVE's own updater (M-UPGRADE). V16 brings its own boot area, which only
  M-VAVE's tools change; no community installer goes from V16.
- **V15 -> TransitFM1** (github.com/Quixotic7/TransitFM1, 0.1.4+): the riskiest hop, once per unit. Check the
  package against the SHA-256 its site publishes.
- **TransitFM1 -> the modes firmware**: TransitFM1's loader writes its first sector last, so a cut-off install comes back
  in update mode. Install `firmware/fm1-modes/build/modes.fwsc` with `fm1_install.py` (or TransitFM1's web
  installer).
- **The modes firmware -> a newer one**: fm1-base's loader resumes a cut-off install, but writes in address order. While
  the firmware is young, go back through TransitFM1 for each update.

Before asking for the yes, show: `--info` output, the package path, `shasum -a 256` of it, its identity, and the
route step. After the install: `--info` must give the new identity, then `fm1_remote.py screen` and `log`.

## After installing the modes firmware

- `fm1_remote.py screen` should read the running mode's screen; `log` shows the supplies and any crash.
- Play a few keys and chords; watch for crashes (`crashes --elf ...`) and for xruns, which mean the engine is
  over its CPU budget. A crash or a black screen: go to "Back to stock" before anything else.
- Record what was seen (CPU, sound, screen) in `docs/research/fm1-firmware-development.md`.

## If an install is cut off

Switch the FM-1 off and on **once**. TransitFM1 and fm1-base firmwares come back in update mode (three short
flashes, `ota-FM-1` on USB): run the same `fm1_install.py` command again; it finishes the write. If the unit then
shows a black screen and the computer sees `WL82 UBOOT1.00`, it is in ROM boot: use the Unbricker or fm1_rescue to
put V15 back, with Ian's yes. Nothing on USB at all: the Transporter route. Stop and tell Ian at each step.

## Back to stock

The official V15 package: `FM-1_015.fwsc`, 699,956 bytes, SHA-256
`db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`. M-VAVE ships it under Apache-2.0 in FM1 Open
(github.com/m-vave/fm1_open, `sdk/firmware/FM-1_015.fwsc`); M-VAVE's own download URL now serves V16. Install it
with `fm1_install.py` from any fm1-base or TransitFM1 firmware, after the usual yes. Back on V15, presets may read
garbled after Felucca-family firmwares: System > Reset Patches.

Background and sources: `docs/research/fm1-firmware-development.md`.
