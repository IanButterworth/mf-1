---
description: Install a firmware package on the connected FM-1 by the safe route, with a confirmation before each write
argument-hint: "[package.fwsc] (default: firmware/fm1-modes/build/modes.fwsc)"
---

Install the package "$ARGUMENTS" (empty means `firmware/fm1-modes/build/modes.fwsc`) on the connected FM-1,
following the `fm1-device` skill step by step. Its rules are not optional:

1. Read first (as `/fm1-info`): what the unit runs now, and confirm the answer comes from the hardware.
2. Work out the route from there to this package (V16 -> V15 -> TransitFM1 -> the modes firmware). If a hop is needed, that
   hop is its own install with its own confirmation.
3. Check the unit has a full-flash backup in `~/Documents/GitHub/mf-1-ref/fm1-backups/`. If not, stop and run
   `/fm1-backup` first.
4. For a modes firmware package, check it passed `/fm1-check`'s device build in this session; if not, run it.
5. Show Ian: the current identity, the package path, its SHA-256 and identity, the route step and the recovery plan
   if it is cut off. Ask, and wait for an explicit yes in chat. Then run the one install.
6. After it: `--info` must give the new identity; then `fm1_remote.py screen log`. Report what happened.

If the installer exits with 4 or 5, or anything looks wrong, do not retry: follow the skill's "If an install is
cut off" and tell Ian.
