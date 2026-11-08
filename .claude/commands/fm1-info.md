---
description: Read what the connected FM-1 runs and how it is doing, without writing anything to it
---

Follow the `fm1-device` skill. This command only reads: never install, restore or write.

1. Make sure MF-1 is not running (its virtual port is named like an FM-1); if it is, ask Ian to quit it.
2. From `firmware/fm1-base`: `$PY tools/fm1_install.py --info`. Report the identity and what it means (stock V15,
   V16, TransitFM1, the modes firmware with its build, another firmware).
3. If it runs an fm1-base firmware (the modes firmware): `$PY tools/fm1_remote.py screen log`, and
   `$PY tools/fm1_remote.py crashes --elf ../fm1-modes/build/modes.elf` when that ELF matches the installed build.
4. Say whether anything needs attention (a crash, low battery, update mode), and what the next safe step would be.
