# MF-1: agent instructions

MF-1 is a native macOS emulator of the M-VAVE FM-1 pocket FM synthesizer. Read this before
changing anything.

## Goals

1. **Honest parity with the hardware.** The FM-1 should look, sound and behave the same in
   MF-1: the same panel, screens, pages, knob mappings, button and LED behaviour, presets, MIDI
   and SysEx. When the hardware does something odd, reproduce it and document it. Never
   "improve" behaviour silently.
2. **Patch loading the way the community does it.** Anything that works with a real FM-1 over
   MIDI should work with MF-1's virtual port:
   - Web librarians and editors.
   - Dexed parameter changes.
   - DX7 bank and voice dumps.
   - M-VAVE's own restore tool and its private `00 59` protocol.
3. **Performance and latency.**
   - The audio thread never locks, allocates or blocks.
   - Key-to-sound time is kept as low as the output device allows.
   - The UI must not delay input. Redraw only what changes.
4. **Clear about uncertainty.** Where the community's research disagrees or is incomplete,
   say so in code comments, the README's "Known differences" and `docs/research/`. Don't
   guess silently.

## How parity is organised

- The engine is Google's msfa, because the FM-1 firmware runs msfa itself, with tables identical
  byte for byte. Measured deviations of FM-1 firmware from msfa sit behind one switch
  (`fm1Quirks`, Device › FM-1 V15 Hardware Behaviour, on by default). With it off the engine is
  clean msfa.
- Each quirk must cite its measurement source in `docs/research/fm1-protocol-and-engine.md`, and
  should come with a check in `mf1-render test` where it can be measured.
- Firmware bugs that only lose data or hang (for example the transpose note-off hang) are
  documented rather than reproduced, unless the user asks otherwise.
- **Faithful and Extended modes.** Faithful (the default) must sound exactly as the hardware.
  MF-1-only extras (for example stereo width) belong in Extended mode only. They may act only after
  the FM-1's signal path, and they never change what a patch's FM-1 data means: a patch shaped in
  MF-1 must sound the same on a real FM-1. Never get a sound by adding processing to the
  Faithful path. If a sound is out of the FM-1's reach, say so and use Extended for the extra.
- The effect DSP is not published. The six effects are generic designs fitted to the published
  CC ranges and the community's measurements; update them when better measurements appear.
- Bank E (presets 129-160) is an Extended-mode extra. Faithful mode has the FM-1's 128 presets,
  and the M-VAVE vendor protocol and MIDI program changes only ever address those 128.

## Voicing patches and songs

- Shape sounds only with what a real FM-1 has: DX7 voice parameters, its six effects, ENV,
  mono/poly and glide. Remember that `fm1Quirks` changes what some parameters do (detune is about
  2.8 cents a step on V15, feedback is weaker); say in the patch comment when a patch relies on it.
- When matching a recording, measure two things: the long-term 1/3-octave spectrum and how
  brightness and level change over a held note. An averaged spectrum alone hid a decaying,
  mallet-like strike in RIGHTPLACE. The write-up of that process:
  https://claude.ai/artifact/2CMWHoXnZFVhMnmpkKdhbH
- Built-in songs (`songs.cc`) may contain chord parts and arrangements, not note-for-note
  transcriptions. Never commit copyrighted MIDI files or recordings. Users add their own MIDI
  files, and a file named like a built-in song replaces its notes.

## Keeping up with the community

The FM-1 is new (2026) and people are still reverse-engineering it. Firmware updates change
behaviour, and measurements get refined. **Check the sources below periodically, and whenever
starting a substantial piece of work.** Then update MF-1 and `docs/research/` to match.

Check:
- M-VAVE firmware release notes, for new firmware versions and features:
  https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1.txt
- M-VAVE's MIDI implementation document:
  https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1%20MIDI%20EN.docx
- The user manual: https://manualf.oss-cn-hongkong.aliyuncs.com/manual/MIDI-KEYBOARD/FM-1-manual.pdf
- Community index: https://github.com/cicloid/awesome-fm-1 (start here; it links new projects).
- Firmware reverse engineering: https://github.com/AL-255/FM-1-RE
- Hardware measurements: https://github.com/thegiantsnail/fm1-workbench-public (`FINDINGS.md`)
  and https://github.com/jbschooley/Virtual-FM-1 (`docs/HARDWARE-NOTES.md`).
- Factory presets and protocol captures: https://github.com/KingParamount/fm1-factory-presets
- Librarian and evidence ledger: https://github.com/benny-sparra/fm1-dx7-patch-importer
- SysEx tools: https://github.com/czietz/fm1-read-voice
- Discussion: the Elektronauts thread (https://www.elektronauts.com/t/m-vave-fm-1/252170) and
  the KVR thread (https://www.kvraudio.com/forum/viewtopic.php?t=632127).

When something new turns up:
1. Record it in `docs/research/` with its source URL, firmware version and confidence:
   confirmed, measured, inferred or conflict.
2. If it changes behaviour, implement it. Put firmware deviations behind `fm1Quirks`, and name
   the firmware version in the comment or commit.
3. Add or update a check in `Sources/mf1-render/main.cc` when the behaviour is testable offline.
4. Update the README's "Known differences" and the open questions in `docs/research/`.
5. Note the date of the check in `docs/research/README.md` (create it if missing), so the
   next check knows what is already covered.

Licences matter when reading community code:
- msfa (Apache-2.0) and MIT, WTFPL, Unlicense or CC0 code can be reused, with notice.
- Dexed, Virtual-FM-1, melodee and fm1-workbench are GPL. Take facts and measurements from them,
  never code, so MF-1 stays MIT.
- Never commit M-VAVE firmware images.

## Layout

- `Sources/MF1Core` (C++17 and Objective-C++):
  - `dsp/`: msfa-derived operators, envelopes and LFO (Apache-2.0 headers stay).
  - `synth.*`: the audio-thread synth (voices, mono and glide, ENV, arp, sequencer, clock,
    output stage).
  - `fx.*`: the effect chain.
  - `songs.*`: built-in songs with their voices; `midifile.*`: Standard MIDI File reading.
  - `engine.cc`: the model, persistence, MIDI and SysEx handling, and the C API in
    `include/MF1Core.h`.
  - `platform_*`: CoreAudio and CoreMIDI.
- `Sources/MF1` (SwiftUI and AppKit): the panel, laid out in millimetres from a top-down photo;
  the 240x240 screen (`ScreenView`, with `ScreenLayout` shared by drawing and clicks); the device
  state machine (`DeviceModel`); the app, window and menus; `Engine.swift` has the Swift helpers,
  the song library and the audio output list.
  - `PanelView` is generic over `PanelDriver` (`PanelDriver.swift`): `DeviceModel` drives MF-1,
    `IScreamModel` drives iScream. Anything new that the panel needs from its model goes on the protocol.
  - iScream (`IScreamModel`, `IScreamScreen`, `IScreamApp`; `main.swift` picks the front end) is a mode
    of the same app (View › iScream Mode, or `--iscream`), picked at launch. Keep MF-1's behaviour unchanged when editing shared panel code,
    and check both with `--snapshot`.
- `Sources/IScreamCore` (C++17): iScream's flavors, how a sundae becomes an FM-1 voice and effects. It
  uses only the public C API and only what a real FM-1 has (see `docs/iscream.md`).
- `Sources/mf1-render`: headless checks (`main.cc`, `tests_engine.cc`, `iscream_tests.cc`), benchmark
  and WAV rendering through the offline C API.
- `docs/research/`: the specs the emulation is built from.
- `firmware/iscream`: iScream as firmware for the real FM-1, on fm1-base (`firmware/fm1-base`, a submodule). Its
  engine is a C port of MF-1's, kept bit for bit equal by `firmware/iscream/tests/run_parity.sh`; a change to
  MF1Core's msfa code or to IScreamCore's flavors needs the parity test too. Build and test it with the
  `fm1-firmware` skill. Anything that touches a connected FM-1 goes through the `fm1-device` skill, which asks
  before every write.

## Build and test

```sh
scripts/build-app.sh                                          # build/MF-1.app
swift build -c release
.build/release/mf1-render Sources/MF1/Resources/Banks test    # must pass before committing
.build/release/mf1-render Sources/MF1/Resources/Banks bench   # CPU must not regress
MF1_LATENCY_LOG=1 build/MF-1.app/Contents/MacOS/MF-1          # per key latency breakdown
build/MF-1.app/Contents/MacOS/MF-1 --snapshot out.png fx      # offscreen panel render
```

Other `mf1-render` modes: `wav <preset> <out> [note]`, `song <n> <out> [s]`,
`songenv <n>` (envelope of a song's voice), `midi <file> <voice song> <out> [s]` and
`midi-extended …` (stereo, Extended mode on).

- When testing the running app, mute MASTER first (drag it down), so test notes don't play aloud
  on the user's speakers or headphones. Put it back afterwards.
- Ian's machine has small helper tools in `~/Documents/GitHub/mf-1-ref/tools/` (not in the repo):
  find the window, click or drag at a point on the panel, press a Mac key, hold a MIDI note, and
  listen to the MIDI port. Reference photos, the manual and the recording are in that folder too.
- Offline tests render in 64 sample blocks when they compare against the scope or block timing;
  other chunk sizes leave the block position out of step.
- The saved state (`~/Library/Application Support/MF-1/state.bin`) is the user's data. Before
  changing its format, back it up, bump `kStateVersion`, and migrate the older version on load;
  never let an older file become unreadable. Unreadable files are kept aside, never overwritten.

## Releases

1. Bump `VERSION` in its own commit, push, and run the tests.
2. `scripts/make-dmg.sh` builds a universal, ad-hoc signed app into `build/MF-1-<version>.dmg`.
   Mount it and check the architectures, version and signature.
3. Tag `v<version>` on the bump commit, then `gh release create` with the DMG and notes that start
   with the `Claude:` preface. Download the asset back and compare its SHA-256.

There is no Developer ID certificate, so releases are not notarised; the README and the notes
tell users to right-click › Open on first launch.

## UI pitfalls

- Menu settings must use the `applying(_:_:)` bindings in `MF1App.swift`. `.onChange` on a menu
  item runs only when the menus are next rebuilt, one change late.
- The App scene must not observe `DeviceModel`, which publishes often. LEDs and the screen
  redraw through `LedClock`/`Ticking`, and only when something visible changes; anything a
  `Ticking` view draws must be one of its inputs, or SwiftUI skips the redraw.
- Fonts are registered in the App's `init`, before any view draws.
- The panel is an AppKit `PanelWindow` owned by the app delegate, so it can be borderless and
  still take keyboard focus. The SwiftUI scene is a hidden menu bar extra that only carries menus.

## Conventions

- Commit titles: `MF1Core: ...` for the engine, `MF1: ...` for the app. Write a prose body.
- Match the surrounding style. Comments only for a non-obvious why, in plain English, without
  naming identifiers.
- The audio thread may not lock, allocate, log or call into Swift. Send changes through the
  command queue.
- "FM-1" and "M-VAVE" may appear only to describe compatibility. The app is MF-1. The identity
  string "FM-1_015" in vendor replies and the "FM-1" in the MIDI port name are there so
  existing tools recognise the port.
