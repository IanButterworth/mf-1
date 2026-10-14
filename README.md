# MF-1

A native macOS emulator of the M-VAVE FM-1 pocket FM synthesizer: the same panel, screens,
engine and MIDI/SysEx behaviour, so patches, librarians and workflows made for the FM-1 work
with it.

## Build and run

Requires macOS 14+ and Xcode command line tools (Swift 5.9+).

```sh
scripts/build-app.sh        # builds build/MF-1.app
open build/MF-1.app
```

Headless checks and a benchmark:

```sh
swift build -c release
.build/release/mf1-render Sources/MF1/Resources/Banks test
.build/release/mf1-render Sources/MF1/Resources/Banks bench
.build/release/mf1-render Sources/MF1/Resources/Banks wav 1 piano.wav 60
```

## Design

- **Engine** (`Sources/MF1Core`, C++17): the FM-1 firmware runs Google's
  [msfa](https://github.com/google/music-synthesizer-for-android) DX7 core, with tables
  identical byte for byte, so the engine is msfa too. Around it: 12-voice round-robin
  allocation as in the firmware, mono with glide (Full Time / Fingered), the per-patch ENV
  amp envelope, the reorderable 6-slot mono FX chain, the 7-mode arpeggiator, the 16-pattern
  × 16-step sequencer with per-step rate, chains and long notes, and MIDI clock.
- **FM-1 V15 behaviour** (Device › FM-1 V15 Hardware Behaviour, on by default): the
  deviations from msfa measured on real units, from `docs/research/`: detune about 2.8 cents
  per step, feedback at a quarter strength, the FM-1 tremolo depths (AMS 3 silences at each
  trough), vibrato × 0.88, pitch EG 1.37× slower, incoming velocity × 100/127, aftertouch
  vibrato, operator mute key mapping reversed, edits reaching only the next note, 16-bit
  output, 20 Hz high-pass and 5.6 kHz low-pass on the output. Turn it off for a clean DX7.
- **Realtime**: the audio thread renders 64-sample blocks from a Core Audio output unit. It
  takes commands from a lock-free queue and never locks or allocates. 12 voices with every
  effect on use about 0.4% of one Apple-silicon core.
- **UI** (`Sources/MF1`, SwiftUI): the panel is laid out in millimetres from a top-down photo
  of the unit. The 240×240 screen is drawn in screen pixels and scaled.

## Loading patches

The app publishes a virtual MIDI port named **MF-1 (FM-1 Compatible)**. Web librarians and
editors that look for an "FM-1" port find it, and it answers as the hardware does:

| What | How |
|---|---|
| DX7 32-voice bank (`F0 43 0n 09 20 00 …`, 4104 bytes) | the screen asks for a bank: turn KNOB1–4 for A–D; saved at once, and remembered as the "last imported" bank for Reset Patches |
| DX7 single voice (`F0 43 0n 00 01 1B …`, 163 bytes) | loads the edit buffer; hold SAVE to keep it |
| DX7 parameter change (`F0 43 10 gg pp vv F7`) | writes the edit buffer |
| M-VAVE vendor protocol (`F0 00 32 …`, packed `00 59` packets) | identify, voice-store writes (the factory restore tool) and edit-buffer reads |
| FX CCs 0–23 on the FX channel (default 2), PC, pitch bend, CC1/64, aftertouch, clock | as in M-VAVE's MIDI implementation |

You can also drag `.syx` files onto the window or use File › Import, and export banks or the
current voice from the File menu.

## Controls

Drag knobs up or down, or use the scroll wheel (Option for fine steps). Play the keys with the
mouse or the Mac keyboard (A W S E D R F G Y H U J K O L P ; [ ' from F3, Z/X for OCT−/OCT+).
The screen also works by mouse:
- Click a value box to step through short lists of choices (shift-click or right-click steps back). Drag up or down, or scroll, on any box to change its value as with its knob.
- On the FX page, click an effect to select it, then click it again to switch it on or off.
- Click the bottom bar for the next page.
- Click the voice name for a menu of every preset, or drag on the name to scroll through them.

Hold SAVE to save, as on the hardware. The window is frameless by default: drag any bare part of
the case to move it, and use ⌘W or ⌘Q to close. View › Frameless Window (⌃⌘F) switches to a
normal window. Help › MF-1 Keyboard and Controls lists the rest.

## Known differences

- Effects: M-VAVE has not published the effect DSP, so the six effects are generic designs.
  They are fitted to the published ranges and the community's measurements (reverb decay
  times, delay 0.8 s down to 0.1 s, chorus and phaser rates, distortion level).
- Not emulated: Bluetooth, battery, USB audio, firmware updates, and the algorithm 4/6
  routing bug whose trigger is not yet understood.
- The note-off transpose hang, where a note sticks if you change transpose while holding it,
  is not reproduced.
- The audio runs at the output device's rate, not the FM-1's 44118 Hz. Pitch and envelope
  times are scaled to match.

## Licences

- MF-1 is MIT licensed (`LICENSE`), except as listed below.
- `Sources/MF1Core/dsp` derives from msfa, Copyright 2012 Google Inc., Apache License 2.0
  (`LICENSES/Apache-2.0.txt`).
- Fonts: Orbitron and Share Tech Mono, SIL Open Font License (`LICENSES/`).
- Factory banks: see `Sources/MF1/Resources/Banks/README.md`.
- "FM-1" and "M-VAVE" are names of M-VAVE's product, used here only to describe compatibility.
