# Woof

Woof is a dog show on the MF-1 engine, with every key a bark. It is a mode of MF-1: View › Woof Mode switches to it
and back, relaunching the app, and `--woof` starts in it. Like iScream and Almanac, it has its own state folder and
MIDI port ("Woof (FM-1 Compatible)"), so it never touches MF-1's patches.

Each breed is a bark, a place and a mood. The screen is a small pixel-art cartoon of the dog: flat colour set a pixel
off scratchy ink lines, like an old cel. The dog opens its jaw when it barks, points its nose up to howl, wags with
its mood, and trots in when it comes on. The case is white with black spots, and the buttons are collar red.

## The dogs

| Dog | Says | Place | Keys play |
|---|---|---|---|
| Dalmatian | WOOF | Rooftops | single barks; far dogs on the roofs bark back |
| Labrador | WOOF | Park | repeats, an eighth apart |
| Chihuahua | YIP | Parlour | quick repeats: yapping |
| Beagle | AROO | Farm | a single bay, half a howl |
| Husky | AWOO | River | a long howl |
| Bulldog | RUFF | Park | short and gruff, with a growl underneath |
| Poodle | ARF | Parlour | a chorus of two |
| Corgi | BOOF | Farm | a chase of three, back and forth |
| Dachshund | BORK | Park | repeats, a little growly |
| Shepherd | WOOF | Farm | a chorus of three, on guard |
| Great Dane | BWOOF | River | single booming barks |
| Pug | RUFF | Parlour | a snort, then a ruff |
| Otto, a brown cockapoo | WOOF | Park | single friendly barks, a little soft |

## How a bark is made

Every dog is one DX7 voice on algorithm 5, in three stacks:

- **The throat** (OP1 <- OP2): pitched by the key, so the barks can be played as notes.
- **The mouth** (OP3 <- OP4): OP3 runs at a fixed frequency, a formant around 0.6 to 2 kHz that stays put as the
  pitch moves, as a real dog's mouth does. OP4 at the key's pitch spreads sidebands round it.
- **The breath** (OP5 <- OP6): a rough hiss high up, roughened by OP6's feedback, loudest as the bark opens.

A pitch envelope gives each bark its shape: it starts below the key, jumps up as it opens and falls away after.

The barks were matched by measurement to three recordings of medium-sized dogs on Wikimedia Commons ("Barking of a
dog", "Barking of a dog 2" and "Perro ladrando", CC BY-SA, not in the repository). Measured in those recordings:

- Each bark lasts about 130 to 270 ms and is loudest 20 to 90 ms in.
- The fundamental is around 350 to 650 Hz and rises during the bark.
- Most energy is between 400 Hz and 1.6 kHz, with a spectral centroid of 1 to 2.5 kHz.
- The sound is partly rough: the autocorrelation peak is 0.4 to 0.9.

The Dalmatian at F#4 measures 195 ms long and loudest at 20 ms. Its pitch rises about 7 semitones, then falls. Most
of its energy is at 0.4 to 1.6 kHz, with a centroid of 1.1 to 1.5 kHz. FM cannot make true noise, so the roughness
comes from the inharmonic sidebands round the fixed mouth and from feedback on the breath.

## Controls

- **DOG** (the PRESETS knob) picks the breed; `<` and `>` do the same.
- **PLACE** (the ALGORITHM knob) moves the dog: Park, Parlour, Rooftops, Farm or River. Each has its own reverb and
  echo. Outdoors there is no reverb tail: the four outdoor places give only a short reflection off nearby walls or
  trees, and Distance reaches it at a third of its value. The Parlour, the one room indoors, has a small room's
  reverb. Every dog starts dry, with Echo off and reverb only in the Parlour; turn Distance and Echo up for more of
  the place.
- **PAGE** (the SELECT knob) moves KNOB1-4 between four pages. BARK, MOOD, PLACE and PLAY jump straight there.
  - **Bark**:
    - Size moves the pitch by up to an octave, and the mouth with it.
    - Gruff sets the throat's roughness, feedback and drive.
    - Length sets how long each bark lasts.
    - Yelp sets how far the pitch jumps.
  - **Mood**:
    - Mood runs from Mopey to Zoomies. A sad dog's bark opens slowly and falls further; a playful one snaps open
      and jumps higher.
    - Howl turns barks into howls that swell, rise and waver.
    - Growl adds a growl, shaken at random, that carries on while the key is held.
    - Breath sets how loud the huff at the start is.
  - **Place**: Place, Distance (reverb; 0 is off), Echo (dogs far off barking back; 0 is off) and Space (how large
    the place sounds).
  - **Play**: how a key plays.
    - Style:
      - Single: one bark.
      - Repeat: barks again and again while held, on the arpeggiator.
      - Chorus: the pack barks together.
      - Chase: the pack barks in turn.
    - Pack: a pair (a dog a fifth up), a trio (another a fourth down) or a litter of five (puppies above).
    - Tempo and Barks set how fast repeats and chases go.
  - The STYLE and PACK buttons step through the styles and packs.
- **SHOW** (the GLO button) starts a dog show: a new dog trots on every ten seconds, unless a key is held.
- Hold **RESET** (the SAVE button) to put the dog back as bred.
- Every dog keeps how you left it, in `~/Library/Application Support/Woof/dogs.json`.
- On the screen: click a page to show it, click a value to step it, drag or scroll on it to turn it, click the
  picture for the next place, and click the breed name for the list.

## Honesty about parity

- Woof uses only what a real FM-1 has: DX7 voice parameters, the six effects, and the arpeggiator for repeats and
  chases. Faithful mode is always on, and Extended mode off.
- A chorus is made by the front end, which presses several notes for one key, as a firmware would. MIDI notes from
  outside are played as single barks.
- File › Export Dog Bank writes the thirteen dogs as an ordinary DX7 bank. The effects and the playing style are not
  in it.
- The barks were designed with numbers, not by ear: `mf1-render ... woof-report` prints each dog's level, length and
  pitch over a bark. They have not been auditioned on a real FM-1.
- The scene is plain C with integer arithmetic (`Sources/WoofCore/scene.c`), which the firmware compiles too, so
  the FM-1 draws the same pictures.

## On the FM-1

`firmware/woof` builds Woof as firmware for the FM-1, on iScream's port of the engine. Its bark mapping is a C port
of WoofCore, which `firmware/woof/tests/run_parity.sh` checks gives the desktop's voice bytes, effects, repeat
settings and key notes for every value of every field. Repeats and chases play on the same arpeggiator port as
Almanac's patterns. The battery bars show the FM-1's own reading. It runs on one unit: it boots, plays and logged no
crash. CPU load and the repeats' timing on the hardware are not measured yet.

## Tools

- `mf1-render <banks> woof-test`: the Woof checks alone (they also run in `test`).
- `mf1-render <banks> woof-report [note]`: each dog's loudness, bark length and pitch over a dry single bark.
- `mf1-render <banks> woof-wav <dog> <out.wav> <note> [field=value ...]`: three barks of a dog, with any fields set
  by number (as in `WoofCore.h`), to listen to or measure.
- `mf1-render <banks> woof-scene <out.rgb> [place] [bark] [step]`: every dog's scene on one sheet, as raw RGB.
- `MF-1 --woof --snapshot out.png [screen|sheet] [dog N] [page N] [place N] [mood N] [play N] [howl N] [bark 1]
  [step N]`: renders the panel, the screen at four times the size, or every dog's screen on one sheet.
- `MF-1 --woof --selfcheck`: drives the model as the panel would and checks the engine follows.
