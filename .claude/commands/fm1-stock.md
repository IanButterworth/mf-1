---
description: Put M-VAVE's stock V15 firmware back on the connected FM-1
---

Go back to the official V15, following the `fm1-device` skill's "Back to stock".

1. Read first (as `/fm1-info`). If the unit does not start or is not found, use `/fm1-rescue` instead.
2. Get `FM-1_015.fwsc` from M-VAVE's FM1 Open repository into the scratchpad and check it: 699,956 bytes, SHA-256
   `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`. Refuse any other file.
3. Show Ian the current identity, the file and its hash, and ask; wait for an explicit yes. Then install it with
   `fm1_install.py`.
4. Check `--info` gives `FM-1_015`, and remind Ian of System > Reset Patches if presets read garbled.
