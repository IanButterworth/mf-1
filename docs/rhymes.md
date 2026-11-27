# Rhymes

Rhymes is nursery rhymes to learn on the MF-1's keys, made for children of about four. It is a mode of MF-1: View ›
Rhymes Mode switches to it and back, relaunching the app, and `--rhymes` starts in it. Like the other modes it has its
own state folder and MIDI port ("Rhymes (FM-1 Compatible)"), so it never touches MF-1's patches.

The key to play next lights up. A right key plays the next note of the song and moves its picture on; a finished song
earns up to three stars, with a cheer, confetti, a light show running across the keys and a little fanfare, and then
plays itself back in full with a bass line and chords under the tune while its picture replays. Wrong keys
are silent: nothing plays, the song waits, and the right key lights steadily for a moment. The case
is sunny yellow with sky-blue keys, so the keys' white lights stand out.

## The songs

Every song is traditional and in the public domain. The tunes sit on the lower row of keys between G3 and E5, so no
song needs a change of octave, and they are ordered from easiest to hardest.

| Song | Notes | Sound | Picture: each right note… |
|---|---|---|---|
| Hot Cross Buns | E D C | xylophone | takes a bun out of the oven onto the tray |
| Rain, Rain, Go Away | G E A | kalimba | parts the clouds and brings up the sun; at the end, a rainbow |
| Mary's Little Lamb | C D E G | flute | walks Mary and her lamb along the path to school |
| Twinkle, Twinkle | C to A | music box | lights another star round the smiling little star (42 notes, 42 stars) |
| Row Your Boat | C to C | marimba | rows the boat across the river, the oar dipping |
| Are You Sleeping? | G to A | bells | raises the sun and rings the tower bell, until Brother John wakes |
| London Bridge | C to A | toy piano | lays another stone of the bridge |
| Old MacDonald | D to B | banjo | brings the cow in, makes it moo, and adds a chick for every E-I-E-I-O |
| Itsy Bitsy Spider | G to G | harp | climbs the spout, is washed down by the rain, dries in the sun, climbs again |
| Jingle Bells | C to G | sleigh bells | drives the sleigh across the snow; at the end it flies |
| Happy Birthday | G to G | calliope | lights a candle on the cake for each line sung |

Each picture is a pixel-art scene drawn in plain C (`scene.c`), 120 by 64 pixels shown at twice the size, at ten frames
a second.

## The screen

The real screen is about 3 cm across, so nothing on it is small: text is 11 points or more, and nothing in the
pictures that matters is under two of their pixels (four on the screen).

- **The title** in the song's colour, and the best stars the song has earned at this level.
- **The picture**, its characters filling a good part of it. Every right note makes what it moved hop and sends a
  burst of sparkles flying out round it. Before the song starts a word over it says what to do ("Press the shining
  key!"); "Listen" shows while the song plays itself, and "Your song!" during the encore.
- **The next four notes**, each a bead in the colour children's bells and tubes use (C red, D orange, E yellow, F
  green, G teal, A violet, B pink) with its letter, and the syllable sung on it underneath. The next note is the big
  one and bobs; a long note is a wider bead.
- **The song's keys**: a little keyboard of the white keys the song uses and one either side, at least eight, so each
  is big. The next key is in its note's colour while its light is on. It can be clicked to play.

## Levels

LEVEL (the ALGORITHM knob), or the MAGIC, GUIDE and SOLO buttons:

- **Magic**: any key plays the next note, so the youngest can play the whole song by pressing anything. The right key
  still lights, to show where it is. A finished song earns one star.
- **Guide**: the lit key plays the next note and the song waits for it; other keys are silent. Stars: three with a
  wrong key on at most one note in ten, two with at most one in four, otherwise one. Two wrong keys on the same note
  count once.
- **Solo**: the first note is lit; after that the light comes on only when the child waits two and a half seconds, or
  straight after a (silent) wrong key. Stars as in Guide.

## Lights

How the next key shows (LIGHTS knob, or the LIGHT button):

- **Blink**: on and off. After a right key the next one stays dark for 0.2 s first, so a note played twice in a row
  shows as a second blink.
- **Steady**: lit until it is played, with the same short gap.
- **Ahead**: blinking, with the key after it lit dimly. The FM-1's key lights have one colour and three brightnesses
  (off, dim and lit), so the next key and the one after are told apart by brightness, not colour.

After a wrong key the right one is lit steadily for 1.2 s. A finished song sends a light running up and down the keys.

## The encore

Three seconds after a song is finished, it plays again by itself at its tempo (Speed applies), the tune with its
chords under it: at the start of each chord and every half bar, the root as a bass between E2 and D#3 and the third
and fifth (the seventh, for a seventh chord) between E3 and D#4, below the tune. The tune's keys light as it goes and
the picture replays from the start. Keys pressed meanwhile play along freely. Each song's chords are written beside
its tune (`C:4 G7:4 …`, a root, `m` or `7`, and a length in eighths) and must last exactly as long as its notes. It is
all one voice, as the FM-1 has one sound at a time, so the chords are in the song's own instrument.

## Controls

- **SONG** (the PRESETS knob), or `<` and `>`, picks the song.
- **LINE** (the SELECT knob) moves to the start of the line before or after, to practise one. A song started part way
  through earns one star.
- **LISTEN** (PLAY/STOP) plays the song through at its tempo with each key lighting as it plays; any key stops it and
  starts playing. A click on the picture does the same.
- **AGAIN** (SAVE) starts the song from the beginning; holding it puts the song's settings back as written.
- **KNOB1-4**: Speed (50 to 150 % of the song's tempo when it plays itself), Room (how much reverb; 0 is none), Sound
  (any of the eleven songs' instruments; the SOUND button steps through them) and Lights.
- Program changes pick songs 1-11. MIDI notes play the song's sound but do not move the game on: they reach the
  engine directly, as in the other modes. Device › Clear Every Star starts the stars again.

## How the sounds are made

Each sound is one DX7 voice of classic FM recipes, heard through the FM-1's reverb and, for a few, a little chorus. The
mallets (xylophone, marimba, kalimba) are a carrier at the key's pitch with a short, inharmonic knock above it on
algorithm 5; the bells and music box add partials at 2.76, 3.5 and 7 times the key; the flute has a breath chiff from
a feedback operator and a delayed vibrato; the banjo is twanged by feedback; the calliope is six pipes on algorithm 32.
The music box, toy piano and sleigh bells sound an octave up. Every sound is within 3.4 dB of the others at its
loudest. File › Export Sound Bank writes them as a DX7 bank for a real FM-1, one song per slot.

## The game in code

The songs, the game, its lights, the sounds and the pictures are plain C with no C library (`songs.c`, `game.c`,
`voices.c`, `scene.c` in `Sources/RhymesCore`), so the FM-1 firmware can build the same files. A tune is written as
text, notes and words side by side:

```
"e2 d2 c4 e2 d2 c4 / c1 c1 c1 c1 d1 d1 d1 d1 / e2 d2 c4",
"Hot cross buns! Hot cross buns! / One a pen-ny, two a pen-ny, / hot cross buns!"
```

A note is a letter (lower case from C4, upper case from C5, a comma for the octave below) and its length in eighths;
the words have one syllable per note, split at spaces and hyphens, and `/` ends a line in both. A song whose notes and
syllables do not match is refused, and `mf1-render test` checks every song.

The game keeps time in milliseconds. When a song plays itself, or a finished song plays its fanfare, the notes are due
at exact times; on the desktop a thread of the game's own (`rhymes.cc`) sleeps until the next one is due and presses
the engine's keys, so a listened song keeps its tempo to the millisecond. The panel's keys go straight to the game and
then to the engine, with nothing added in between.

`mf1-render <banks> rhymes-test` runs Rhymes' checks; `rhymes-sheet <out.rgb>` draws every song's picture at the
start, half way and finished.
