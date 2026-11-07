---
description: Take a full 1 MiB flash backup of the connected FM-1 through the chip's ROM boot, writing nothing
---

Take a full-flash backup, following the `fm1-device` skill's "Before the first install on a unit".

1. Read first (as `/fm1-info`) and note what the unit runs.
2. The backup needs the chip's ROM boot (`WL82 UBOOT1.00` on USB). Prepare the packages, the loader and the venv
   as the skill says. Then give Ian the commands to run himself: fm1_rescue in his own Terminal (it needs sudo),
   then the skill's `fm1_softkey.py` while fm1_rescue waits. Say what he should see. Do not send the key yourself.
3. The read-only tool: `sudo $PY bsp/tools/fm1_rescue.py FM-1_015.fwsc FM-1_016.fwsc --loader wl82loader.bin`,
   without `--write`. It prints the chip key and flash chip, and saves the image. Never pass `--write` here.
4. Take a second read the same way, and check it is identical to the first.
5. Save the image under `~/Documents/GitHub/mf-1-ref/fm1-backups/` as `YYYY-MM-DD-<what it ran>-<flash id>.bin`,
   with its SHA-256 beside it. Never inside the repo.
6. Report the flash chip (Puya `0x856014` or UC25HQ80 `0xB36014`) and what it means for the install route.
