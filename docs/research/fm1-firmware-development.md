# FM-1 custom firmware development

Research date 2026-11-07. Confidence labels as in `fm1-protocol-and-engine.md`.

## How firmware gets onto an FM-1

- Every stock update and every community installer goes over USB-MIDI SysEx, using the update protocol in
  `fm1-protocol-and-engine.md` §1.5.6. The `.fwsc` package has no cryptographic signature [confirmed, S2].
- The flash has a single bank and the board has no debug header. A failed install can leave a unit that no
  USB-MIDI installer reaches. The way back is the AC791N's mask-ROM USB download mode, reached with a dongle
  (an RP2040 on the USB data lines, FM-1 Transporter; or a Flipper Zero) or with tools that send the unit there
  from software when it still enumerates (MvaveFM1Unbricker, M-Flasher, the Firmware Hub's `fm1_rescue.py`)
  [awesome-fm-1, recovery section].
- FM-1s sold since autumn 2026 have a UC25HQ80 flash chip (JEDEC `0xB36014`) in place of the Puya
  (`0x856014`). A firmware that knows only the Puya keeps its saves in RAM and refuses later installs, so the
  unit then needs the download mode [Felucca issue #320; Dinghy 0.5 README; measured by those projects].
- Community firmwares expect stock V15 as the starting point and offer it as the way back.

## Factory V15 is not the published V15

A unit that left the factory with V15 runs a different build from M-VAVE's published `FM-1_015.fwsc`
(699,956 bytes, SHA-256 `db1642b2…`), though both report `FM-1_015`.
- jvitkauskas/fm1-backup: a double-read full dump of a stock unit (Puya `0x856014`, chip key `0x980F`). Its
  installed `app.bin` is 580,892 bytes (CRC16 `BD93`); the package's is 581,564 (CRC16 `5D48`). No published
  package matches it [measured by that project].
- Our unit (Puya `0x856014`), full flash read 2026-11-07 with fm1-base's `fm1_rescue.py`, read only:
  - The flash head below `0x4000` equals the package's.
  - 143 of the 147 sectors in the app area differ from the package.
  - Decoded with the SFC cipher (chip key `0x980F`, block addresses counted from `0x4000`), both hold the same V15:
    the same SDK build stamps (`DRIVER ...@20231109-$9aaf4e5`, `UPDATE ...@20230817-$2374938`) and 980 of 995
    text strings in common. On the unit the code sits `0x2A0` bytes lower, the 672 bytes between the two sizes
    above. The cipher mixes the block address into its key, so this shift alone makes almost every sector
    differ. The shift is `0x2A2` in places, so the builds also differ slightly in content [measured].
- So a return to stock with the package installs a different V15 build than the unit shipped with. Only a full
  flash backup keeps the factory build. A sector-diff restore rewrites nearly the whole app area, not a few
  sectors [inferred].
- Getting there: OCT- and OCT+ held at power-on did not reach the ROM boot from V15. The SysEx key
  `F0 22 24 35 7D F7` did, sent three times 20 ms apart after an identity query, with the port opened at least
  0.3 s before the first send (CoreMIDI may drop a message sent just after opening). The key and the sequence
  are MvaveFM1Unbricker's `softkey` [measured on our unit].

## Routes to a firmware of your own

| Route | What it gives | Licence | Build host | Notes |
|---|---|---|---|---|
| fm1-base (charlesvestal) on felucca-bsp (hugelton) | Platform layer above the hardware: screen and fonts, panel, LEDs, audio, MIDI over USB and TRS, storage, update and rescue, the screen reader. A host simulator and a browser emulator run the same app | MPL-2.0 per file; the app may use any licence | Linux x86-64; Docker on macOS | Beta. App interface in `include/plat.h` |
| Dinghy, foma | Smallest working firmware on Felucca's platform; foma wraps it in one C file and a Docker image | GPL-3.0 | Docker | Dinghy 0.5 knows the UC25HQ80 |
| FM1 Open (M-VAVE) | M-VAVE's own instrument engine as a binary library (`libfm1_engine.a`, DX7 core, 303, 808, effects) under a C SDK | App GPLv3 with an SDK linking exception; the engine may run only on FM1 devices | Windows, JieLi toolchain 2.5.3 | Its app is an eight-track groovebox with its own effects, not V15's six-effect chain |
| A Felucca fork | A full firmware with an msfa 6-op engine | GPL-3.0 | Docker | The base of most published firmwares |

Facts that shape the code, from fm1-base's README and `include/fastmath.h` [measured by those projects]:
- The CPU (pi32v2) has a single-precision FPU and no fused multiply-add. `double` and libm are not allowed;
  fm1-base's `fastmath.h` provides the maths. A host build with `-ffp-contract=off` computes the same samples.
- The audio callback renders 24-bit stereo at 44.1 kHz, in blocks the app chooses (fm1-base's template: 256).
- About 581 KB of flash for the app slot, 96 KB of RAM for `.data` and `.bss`, and a 336 KB pool for large
  buffers.
- fm1-base builds C only, as one unity build plus optional `-O2` units for DSP.

## iScream's route

iScream's firmware is in `firmware/iscream`, on fm1-base (a git submodule). Reasons:
- fm1-base is MPL-2.0, so the iScream app stays MIT like the rest of MF-1.
- It has a host simulator, so the firmware can be built and heard on a Mac before any hardware is involved.
- It knows both flash chips and comes with the update and rescue path.

### First install on hardware (2026-11-07)

Our unit (factory V15, Puya `0x856014`), with `fm1_install.py` from the Mac:
- V15 to TransitFM1 0.1.4 (`transit-0.1.4.fwsc`, SHA-256 `3ba85410…`): a classic install, exit 0. `--info` still
  gives `FM-1_015`, as designed; the USB serial number reads `TransitFM1-0.1.4`. The TRANSIT screen, no red line.
  TransitFM1's own SysEx query (`F0 7D 54 46 31 01 F7`) got no answer in 1.5 s; not looked into.
- TransitFM1 to iScream (`iscream.fwsc`, `FM-1_4700`, build `DAA5BFE7`, SHA-256 `6d7b85a6…`): a classic install,
  exit 0, `--info` gives `FM-1_4700`. The screen shows Vanilla, and keys play without audible dropouts. `fm1_remote.py
  log` reports no crash after playing.
- Not yet measured: CPU load at 12 voices with effects, xruns, output level, the screen frame rate. `fm1_remote.py
  screen` prints nothing until iScream announces a change, so it reads empty right after boot.

The firmware does not use M-VAVE's engine. It ports MF-1's msfa core and effects to C, so on hardware it sounds like
MF-1 in Faithful mode, not literally like the stock V15 firmware. Where the two differ is listed in
`fm1-protocol-and-engine.md` §2.

## Sources

- awesome-fm-1: https://github.com/cicloid/awesome-fm-1 (index; recovery and development sections)
- fm1-base: https://github.com/charlesvestal/fm1-base (README, DESIGN.md, LICENSING.md, template)
- felucca-bsp: https://github.com/hugelton/felucca-bsp
- Felucca: https://github.com/hugelton/Felucca and issue #320 (flash chip)
- Dinghy: https://github.com/zvenson/dinghy ; foma: https://github.com/konsumer/foma
- FM1 Open: https://github.com/m-vave/fm1_open
- setup-fm1-toolchain (Linux toolchain and AC79 SDK mirror): https://github.com/DimmKirr/setup-fm1-toolchain
- The Firmware Hub (installer and rescue): https://thefirmwarehub.com/
- jvitkauskas/fm1-backup (factory V15 dump): https://github.com/jvitkauskas/fm1-backup
- MvaveFM1Unbricker (ROM boot key, double-read backup): https://github.com/Quixotic7/MvaveFM1Unbricker
