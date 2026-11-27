# Rhymes for the FM-1

Rhymes' songs, game, sounds and pictures for `../fm1-modes`: nursery rhymes for small children, the next key lit. See
`../../docs/rhymes.md` for the design. Its panel, the keys' lights and the screen are `../fm1-modes/modes/rhymes.c`.

| Path | What |
| --- | --- |
| `dsp/songs.c`, `dsp/game.c`, `dsp/voices.c`, `dsp/scene.c`, `dsp/internal.h`, `dsp/RhymesCore.h` | The desktop's `Sources/RhymesCore`, copied unchanged by `tools/gen_rhymes.sh`; do not edit |
| `tests/run_parity.sh` | The copies against the desktop's files; the copy built as C99 without warnings; every song played, lit, voiced and drawn the same unoptimised and optimised |

The game's own notes (a song listened to, the fanfare) start and stop in the audio interrupt, on a clock counted from
its blocks, so they keep their time whatever the screen is doing. To build and install the firmware, see
`../fm1-modes/README.md`.
