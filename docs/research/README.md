# Research

- `fm1-protocol-and-engine.md`: the MIDI, SysEx and vendor protocols, the sound engine and its
  measured deviations from msfa, firmware facts, factory presets, and the licences of
  community sources.
- `fm1-hardware-ui.md`: panel layout and colours, screen pages, and the behaviour of every
  control, from the official manual and photos.
- `fm1-firmware-development.md`: how to write and install custom firmware for the FM-1, and the
  route iScream's firmware takes.

## Community check log

Add a line each time the sources listed in `AGENTS.md` are checked. Say what was new and what
changed in MF-1 as a result.

- 2026-10-14: first survey. Firmware up to V15 (2026-07-30). Sources: the M-VAVE manual,
  release notes and MIDI document; awesome-fm-1, FM-1-RE, fm1-workbench FINDINGS,
  Virtual-FM-1 hardware notes, fm1-factory-presets, fm1-read-voice and fm1-dx7-patch-importer;
  Synth Anatomy, Tao of Mac, Elektronauts and KVR. Finishes from retailer listings: Black,
  Purple, Orange, Black & Green, White & Grey, White & Blue, White & Green (Tankpedal, Walmart). Open
  questions are in `fm1-protocol-and-engine.md` section 8.
- 2026-11-07: firmware development survey. New: official V16 (2026-10-21: eight Voices, 20-voice
  polyphony, an FX library and FX CCs moved to 70-93); M-VAVE's FM1 Open repository and SDK, which also
  puts the V15 image under Apache-2.0; about 30 community firmwares, mostly from Felucca; the
  fm1-base and felucca-bsp platform layers; newer units with a UC25HQ80 flash chip. MF-1 still
  emulates V15 (open question 9). Started iScream's firmware on fm1-base (`fm1-firmware-development.md`).
  Sources: awesome-fm-1, FM1 Open, Felucca, felucca-bsp, fm1-base, Dinghy, foma,
  setup-fm1-toolchain, the release notes, fwradar, Drey Andersson and Synth Anatomy.
