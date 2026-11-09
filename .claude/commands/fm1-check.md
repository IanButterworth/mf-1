---
description: Build and test the modes firmware (iScream, Almanac, Woof, Phase) - parity with MF-1, the host simulator, the device build and its safety checks
argument-hint: "[parity|host|device] (default: all three)"
---

Follow the `fm1-firmware` skill's "The checks, in order". Which checks: "$ARGUMENTS" (empty means all three).

- Run the checks in order and stop at the first failure; show the failing lines.
- Host simulator screenshots go in the scratchpad; look at them, scaled up.
- For the device build, quote its `ok` lines and say plainly whether the package is safe to hand to `/fm1-install`:
  it is not if any check names functions (the 64-bit multiply hazard) or a limit is exceeded.
- Write nothing to a connected FM-1.

End with one line per check: passed or failed, and the package path and SHA-256 if the device build ran.
