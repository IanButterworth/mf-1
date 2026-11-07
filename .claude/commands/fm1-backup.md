---
description: Take a full 1 MiB flash backup of the connected FM-1 through the chip's ROM boot, writing nothing
---

Take a full-flash backup, following the `fm1-device` skill's "Before the first install on a unit".

1. Read first (as `/fm1-info`) and note what the unit runs.
2. The backup needs the chip's ROM boot (`WL82 UBOOT1.00` on USB). Ask Ian to put the unit there by hand, saying
   what he should see. Do not try to send it there by software.
3. Run a read-only tool: `sudo $PY bsp/tools/fm1_rescue.py V15.fwsc V16.fwsc` without `--write` (it needs pyusb
   and libusb-package in its venv; it prints the chip key and flash chip, and saves the image), or
   MvaveFM1Unbricker's verified double read. Never pass `--write` here.
4. Save the image under `~/Documents/GitHub/mf-1-ref/fm1-backups/` as `YYYY-MM-DD-<what it ran>-<flash id>.bin`,
   with its SHA-256 beside it. Never inside the repo.
5. Report the flash chip (Puya `0x856014` or UC25HQ80 `0xB36014`) and what it means for the install route.
