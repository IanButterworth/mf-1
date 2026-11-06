# iScream

iScream is a small synth made of ice cream, running on the MF-1 engine. It is the same binary as
MF-1 under another name: the app is iScream when its executable or bundle identifier says so, or when
started with `--iscream`. `scripts/build-iscream.sh` builds `build/iScream.app`.

The idea: no menu diving. You turn **FLAVOR** and get a different, good-sounding voice. The screen
is the sundae you are building, and the knobs shape it.

## What it is made of

| Ice cream | Sound | In the engine |
|---|---|---|
| Flavor (scoop) | the oscillator and its envelopes | a DX7 voice, algorithm 5 (three 2-operator stacks) |
| Scoops (1 to 3) | how wide it is | detune between the three stacks |
| Sweet | brightness | how hard the modulators push (modulator output levels) |
| Melt | how long it hangs on | release rates, and decay rates for the plucked flavors |
| Fizz | vibrato | LFO pitch depth |
| Cone (filter) | tone | the Filter effect: waffle low pass, sugar band pass, cup high pass |
| Toast, Crunch | cutoff, resonance | filter cutoff and Q |
| Whipped Cream | chorus | Chorus: mix, rate, depth |
| Hot Fudge | distortion | Distortion: gain, tone, level |
| Caramel | phaser, plus a worn-tape detune and flutter | Phaser, voice detune and LFO |
| Choc Chips | delay | Delay: mix, feedback, speed |
| Rainbow Sprinkles | reverb | Reverb: mix, decay, room (room, hall or plate) |

The twelve flavors: Vanilla (plain analog saw that widens to a super saw), Chocolate (hollow pulse
with a slow PWM beat), Strawberry (bright pluck), Mint Chip (glass bells), Cotton Candy (airy
shimmering pad), Bubblegum (mono bloop with a pitch drop), Rocky Road (mono bass), Cookies & Cream
(tine electric piano), Butter Pecan (marimba), Lemon Zest (brass stab), Blueberry (deep pad) and Mango
(steel drum).

## Controls

- **FLAVOR** (the PRESETS knob) picks the flavor. `<` and `>` buttons do the same.
- **SCOOPS** (the ALGORITHM knob) sets one to three scoops.
- **TOPPING** (the SELECT knob) moves the focus between scoop, cone and the five toppings. The buttons
  SCOOP, CONE, CREAM, FUDGE, CARML, CHIPS and SPRNK jump straight to one; pressing the one you are on
  switches it on or off.
- **KNOB1-4** change the four values on the screen. KNOB4 is the on/off switch for the cone and the
  toppings. Turning a knob on something that is off switches it on.
- Hold **RESET** (the SAVE button) to put the flavor back as designed.
- Every flavor keeps how you left it, in `~/Library/Application Support/iScream/sundaes.json`.
- On the screen: click a tray icon to focus it (click it again to switch it), click a value to step it,
  drag or scroll on a value to turn it, click the flavor name for the list.

## Honesty about parity

iScream uses only what a real FM-1 has: DX7 voice parameters, the six effects, mono and poly. A sundae
therefore means the same thing on the hardware. Faithful mode is always on and Extended mode off.

- **File > Export Flavor Bank** writes the twelve flavor voices as an ordinary DX7 32-voice bank (the
  remaining 20 slots are empty voices). The cone and toppings are effect settings, not voice data, so on
  a real FM-1 they are set from its FX pages or by MIDI CC. iScream does not yet write them out as a file.
- Voice changes (Sweet, Melt, Fizz, Scoops, Caramel's detune) reach the **next note**, as on the FM-1
  with V15 behaviour on. Effect changes are immediate.
- The sounds were designed with numbers, not by ear: levels, brightness, envelopes and tails are checked
  by `mf1-render ... test` (the iScream group) and `mf1-render ... iscream-report`, which print the
  spectrum and envelope of each flavor. They have not been auditioned on a real FM-1. Expect to retune
  them by ear. Effect DSP is the engine's generic design (see `fm1-protocol-and-engine.md`).
- The flavor loudness check allows 9 dB between flavors at C4, measured as the loudest 100 ms: plucks and
  bells are quieter by that measure than sustained voices.
- Effect and voice edits sent to the port from outside (Dexed, a web editor) are not shown on the screen
  and are overwritten the next time a knob moves a value they touch. Program changes 1 to 12 pick a flavor;
  program changes past that are put back to the current flavor.
- iScream installs its flavors into banks A to D of its own engine state on every launch, so a bank
  stored through MIDI is lost at the next launch. It has its own state folder and MIDI port
  ("iScream (FM-1 Compatible)"), so it never touches MF-1's patches.
- Not done: running as firmware on the real device. The stock firmware cannot run it. The FM-1 community
  OS route (as Felucca does) would need a separate port of the flavor recipes and the screen.

## Tools

- `mf1-render <banks> iscream-report [flavor] [note] [scoops] [toppings]`: loudness, envelope, brightness
  and harmonics per flavor.
- `mf1-render <banks> iscream-env <flavor> <field> <a> <b> <c>`: level over time for three settings of a
  sundae field, for tuning Melt and the like.
- `iScream --snapshot out.png [screen|sheet] [flavor N] [focus N] [scoops N]`: renders the panel, the
  screen at four times the size, or every flavor's screen on one sheet.
- `iScream --selfcheck`: drives the model as the panel would and checks the engine follows.
