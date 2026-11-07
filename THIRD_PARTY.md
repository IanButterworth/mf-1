# Third-party material

MF-1 is MIT licensed (`LICENSE`). These parts are not MF-1's own, or are under other terms. The licence texts
are in `LICENSES/`, and the app carries this file, `LICENSE` and `LICENSES/` in `Contents/Resources`.

| Component | Licence | Copyright | Where |
| --- | --- | --- | --- |
| msfa (Music Synthesizer for Android), the DX7 core: operators, envelopes, LFO, tables. Modified; changed files say so | Apache-2.0 | 2012 Google Inc.; modifications the MF-1 authors | `Sources/MF1Core/dsp/`, `firmware/iscream/dsp/fm.c`, `fm.h` |
| FM-1 V15 factory bank, 128 voices, unmodified (see `Sources/MF1/Resources/Banks/README.md`) | Apache-2.0, as distributed by M-VAVE in FM1 Open | M-VAVE; many voices derive from Yamaha DX7 cartridges and the Dexed_cart compilation | `Sources/MF1/Resources/Banks/*.syx` |
| Orbitron | SIL OFL 1.1 | The Orbitron Project Authors | `Sources/MF1/Resources/Fonts/Orbitron.ttf` |
| Share Tech Mono | SIL OFL 1.1 | 2012 Carrois Type Design, Ralph du Carrois | `Sources/MF1/Resources/Fonts/ShareTechMono-Regular.ttf` |
| fm1-base and felucca-bsp, the firmware platform. A submodule, not copied here | MPL-2.0 (some files also Apache-2.0; see its `LICENSING.md`) | their authors, as listed in each file | `firmware/fm1-base` |

Firmware packages built from `firmware/iscream` also contain JieLi AC79 SDK files (Apache-2.0), downloaded at
build time, and fm1-base's fonts (Barlow Semi Condensed, Terminus: SIL OFL 1.1). Share a package together with the
licence texts fm1-base's `LICENSING.md` lists.

## Names

"M-VAVE" and "FM-1" are M-VAVE's names, and "DX7" is a Yamaha trademark. MF-1 uses them only to say what it is
compatible with. MF-1 is not made, endorsed or supported by M-VAVE or Yamaha. The panel is MF-1's own drawing of
the FM-1's layout, made in code; it uses no photographs and no logos.

So that existing editors and installers accept it, MF-1's MIDI port is called "MF-1 (FM-1 Compatible)" and it
answers M-VAVE's identity query as `FM-1_015`. The iScream firmware answers as `FM-1_47…` with the USB product
"iScream FM-1", and its screen says what it is.
