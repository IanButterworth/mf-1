---
description: Help an FM-1 that will not start, is stuck in update mode, or is not found over USB
---

Work through the `fm1-device` skill's "If an install is cut off", one step at a time, telling Ian what you see and
what comes next before each step.

1. Find out where the unit is: what the screen and key lights show, and what macOS sees on USB (an `ota-FM-1` MIDI
   port means update mode; a `WL82 UBOOT1.00` disk means ROM boot; nothing at all means the Transporter route).
2. Update mode: the same `fm1_install.py` command that was cut off finishes the write. Confirm with Ian first. Never
   a second power cycle while in update mode.
3. ROM boot: back up the whole flash first (read only), then put V15 back with MvaveFM1Unbricker or
   `fm1_rescue.py --write`, after Ian's explicit yes.
4. Nothing on USB: stop. That needs the FM-1 Transporter (an RP2040 on the USB data lines); point Ian to its guide.

Write nothing without a yes for that exact step.
