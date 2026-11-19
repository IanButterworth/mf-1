# Arpeggi

Arpeggi is two arpeggiators on the MF-1 engine, playing one pool of notes together. It is a mode of MF-1: View ›
Arpeggi Mode switches to it and back, relaunching the app, and `--arpeggi` starts in it. Like the other modes it has
its own state folder and MIDI port ("Arpeggi (FM-1 Compatible)"), so it never touches MF-1's patches.

The keys fill the pool. Two lanes, A and B, each play the pool in a shape, at a rate and in a rhythm of their own,
and the relation between them decides how they meet. The screen is a night sky: across the top the pool, slot by
slot; below, every note the lanes strike is a streak at its pitch, A in amber and B in blue, born at the right edge
and drifting left for two and a half seconds, as long as it was held. The case is midnight violet with lilac keys.

## Filling the pool

HOLD (the EDIT button) steps between three ways the keys fill the pool:

- **Hold**: the notes held down, only while held. Letting go of every key stops the lanes.
- **Latch**: a chord stays after the keys let go. Keys pressed while another is held join it; the first key after
  all have let go starts a new chord.
- **Ring**: a fixed number of slots (Ring, 1 to 8). Keys fill the slots in order and stay; once the ring is full,
  each new key replaces the oldest note in its own slot, so a loop played in Order changes one note at a time. The
  same note may sit in two slots. The strip marks, in pink, the slot the next key will fill.

The pool is not a scene's: it stays when the scene changes, so one chord can be heard through every scene. PLAY with
an empty pool starts the scene's own chord. CLEAR empties the pool. With WRITE off, the keys play along over the
lanes and leave the pool alone.

## A lane

- **Shape**: Up, Down, Bounce (up and back without repeating the ends), Order (as the notes came, or slot by slot in a
  ring), Converge (lowest, highest, second lowest...), Thumb (the lowest between every other note), Spiral (up, each
  pass a note higher), Walk (a random walk a note at a time), Shuffle (every note once a pass, in a new order each
  pass), Chord (all at once, an octave up each strike through the span) and Strum (all a moment apart, down and up in
  turn). Span stretches every shape over 1 to 4 octaves.
- **Rate**: 1/2 to 1/32, with dotted and triplet steps, and 1/20 and 1/28: five and seven to a beat.
- **Rhythm**: Every step; 7, 5 or 3 of 8 and 5 of 12, spread evenly (Euclidean); Ratchet (every fourth step three
  times); Gallop (da, da-da); Skip (about one step in three left out, the same ones every 16 steps); Fall (a bouncing
  ball: 12 strikes over 16 steps, each gap 0.8 of the one before). A rest does not skip a note of the shape.
- **Gate**: 5 to 200 % of the step. Over 100 the notes overlap, which is what makes the spacey scenes wash.

## Relations

RELATION (the ALGORITHM knob) decides how B plays against A. DUO (the GLO button) switches B off and on.

- **Together**: side by side, each its own shape, rate and rhythm. Different rates make polyrhythms.
- **Hocket**: one line in A's time, its notes taken in turn from A's shape and B's.
- **Answer**: A plays a bar, B answers the next.
- **Canon**: B plays A's line three of A's steps behind, at B's pitch.
- **Mirror**: B plays A's shape upside down, in B's own rate and rhythm.
- **Trade**: side by side, the lanes swapping shapes every two bars.

B Pitch moves B against A by up to two octaves either way. Values that a relation ignores are greyed on the screen.

## The scenes

| Scene | Relation | Hold | Sound | Space |
|---|---|---|---|---|
| Ring of Five | Together: A Order in 16ths, B Up in triplets an octave up | Ring of 5 | Pluck | dry, small room |
| Clockwork | Hocket: Up and Down sharing one line | Latch | Mallet | bone dry |
| Nebula | Together: Converge in 8ths, Walk in dotted 8ths an octave up, long gates | Latch | Pad | hall 85, echo |
| Call and Answer | Answer: Bounce, then Thumb galloping, swung | Hold | Keys | small room |
| Canon Drops | Canon: a shuffled ring of four, B an octave up | Ring of 4 | Bell | hall, echo |
| Gears | Together: 5 of 8 against triplets an octave down | Latch | Chip | dry |
| Falling Light | Mirror: a bouncing ball, B at half speed | Latch | Glass | hall, echo |
| Trade Winds | Trade: a ratcheting Spiral and a triplet Bounce | Latch | Bass | dry, echo |

The scenes' chords are written for MF-1.

## Timing

The pool and the lanes are plain C (`Sources/ArpeggiCore/arp.c`): integer arithmetic, no allocation, no C library,
and every 64-bit product out of line, so the modes firmware can run the file as it is. Time is counted in ticks,
3360 to a beat, which divides by 2, 3, 5 and 7, so every rate is a whole number of ticks. A lane's n-th strike plays
the n-th note of its shape and falls at a whole multiple of its rate, so where a lane is depends on the elapsed ticks
and the pool alone (`arpeggi-test` checks septuplets a million beats in). Random shapes and Skip use a hash of the
strike count, so they come out the same every time round.

The player (`player.cc`) works as Phase's does: a thread at real-time priority sleeps until the next note on or off,
presses the engine's keys then, and counts notes that the lanes and played-along keys share, so one letting go never
cuts another. A key that finds the clock stopped starts it and plays its first note at once from the main thread,
rather than waiting for the thread to wake. A tempo change keeps the time; any other change, and a new note in the
pool, takes effect from the next strike.

## Controls

- **PLAY/STOP** starts and stops. **CLEAR** (SAVE) empties the pool; hold it to put the scene back as written.
  **WRITE** (REC) switches the keys between feeding the pool and playing along. Space plays and stops.
- **SCENE** (PRESETS) picks a scene; `<` and `>` (ARP, SEQ) do the same. **RELATION** (ALGORITHM) steps the relation
  and switches B on.
- **PAGE** (SELECT) moves KNOB1-4 between five pages; A, B, NOTES, SOUND and TIME (HOME, FX, SEL, ENV, LFO) jump
  straight there.
  - **A** and **B**: Shape, Rate, Rhythm, Gate.
  - **Notes**: Hold, Ring, Span, B Pitch.
  - **Sound**: Sound, Bright, Decay, Room.
  - **Time**: Tempo, Swing (every second step late by up to a third of a step), Echo, Echo Steps (sixteenths, as
    near as the delay's 0.1 to 0.8 s reach allows).
- **OCT-** and **OCT+** move the keys and what is playing.
- Every scene keeps its settings, in `~/Library/Application Support/Arpeggi/scenes.json`.
- On the screen: click a page to show it, click a value to step it, drag or scroll on it to turn it; click the sky
  to play or stop and drag on it to change the relation; click the pool to clear it and drag on it to change the
  ring's size; click the title for the list of scenes.

## Honesty about parity

- The sounds use only what a real FM-1 has: DX7 voice parameters (all on algorithm 5) and its reverb, delay and
  chorus. Faithful mode is always on, and Extended mode off.
- The lanes are the front end pressing keys, as a firmware would. The FM-1's own arpeggiator has one lane and none of
  these shapes, rhythms or relations; Arpeggi turns it off.
- The FM-1 has no velocity per key, so accents are made with rhythm and gate only.
- MIDI notes from outside play straight into the engine; they do not feed the pool, and a note-off from outside can
  stop a note a lane is holding. Program changes pick scenes.
- The sounds were shaped with numbers, not by ear (`mf1-render ... arpeggi-report` prints each sound's level and how
  long it rings). They have not been auditioned on a real FM-1.
- File › Export Sound Bank writes the eight scenes' sounds as an ordinary DX7 bank. The lanes and effects are not in
  it.

## On the FM-1

Arpeggi is the sixth mode of the modes firmware (`firmware/fm1-modes`, D on the chooser). Its scenes and sounds are a
C port (`firmware/arpeggi/dsp`), and its pool and lanes are this same `arp.c`, copied unchanged; that is why the file
needs no C library, makes every 64-bit product out of line, and divides 64-bit numbers out of line too (a division by
a constant would otherwise become a 64-bit multiply, which the FM-1's CPU can get wrong beside a memory load).
`firmware/arpeggi/tests/run_parity.sh` checks every value of every field gives the desktop's voice bytes and effects,
and that the firmware's player starts and releases the same notes as `player.cc` within one engine block: for every
scene for a minute, for a spread of shapes, rates, rhythms and relations, and through keys filling and writing over a
ring, Hold let go, a new Latch chord, a tempo change, a stop, a start and a clear.

- **Where the player runs.** In the audio interrupt, before each of the engine's 64-sample blocks, as Phase's does.
  The ticks are counted from the samples with an exact remainder (3584 × tempo ticks a block at 44.1 kHz). A key
  that starts the clock is heard from the start of the next block. The cost on the FM-1 is not measured.
- **The keys.** While WRITE is on they feed the pool and sound only as the lanes play them; the core calls the mode
  when each key lets go, for Hold. A short press of CLEAR (SAVE) empties the pool and a long one puts the scene back
  as written, as on the desktop.
- **The screen.** The desktop's night sky drawn with the firmware's fills, in 80-row bands: the pool strip, the
  streaks of the last 32 notes, the status, five tabs and the values. While the lanes play it draws frames back to
  back. The fonts are the firmware's (Barlow).
- **Saving.** The scenes' settings and whether the keys feed the arps are kept in a store object of Arpeggi's own.
  The pool is not saved, as on the desktop. Nothing is saved while the lanes play.
- Not on the FM-1: the bank export and the bank prompt, as the modes firmware takes no SysEx. It has run only in the
  host simulator, not yet on a unit.

## Tools

- `mf1-render <banks> arpeggi-test`: the Arpeggi checks alone (they also run in `test`).
- `mf1-render <banks> arpeggi-report [note]`: each sound's loudness and how long it rings, for one short note.
- `mf1-render <banks> arpeggi-wav <scene> <out.wav> [seconds]`: a scene with its own chord, through the offline engine.
- `MF-1 --arpeggi --snapshot out.png [screen|sheet] [scene N] [page N] [relation N] [at BEATS]`: renders the panel,
  the screen at four times the size, or every scene's screen on one sheet, the scene's own chord that many beats in.
- `MF-1 --arpeggi --selfcheck`: drives the model as the panel would, muted, and checks the engine and the player
  follow.
