# Phase

Phase is process music on the MF-1 engine, in the manner of the minimalists' tape and phase pieces of the 1960s and
70s: a short cell played by two or more parts that drift apart, jump, fill in, follow in canon, pulse or stretch, so
that one small phrase keeps turning into new patterns. It is a mode of MF-1: View › Phase Mode switches to it and
back, relaunching the app, and `--phase` starts in it. Like the other modes it has its own state folder and MIDI port
("Phase (FM-1 Compatible)"), so it never touches MF-1's patches.

Play a phrase on the keys and, a moment after the last key, it becomes the cell and the process starts again with
it. The screen is a graphic score in black and red on paper: each part is a ring of its cell's steps, turning under a
fixed red line that is now. Parts that have drifted apart sit rotated against each other, the bar across each ring
marks its first step, and the notes being struck turn red. The case is paper white with black controls and one red
button, PLAY/STOP.

The pieces are all written for MF-1. None uses material from an existing composition.

## The pieces

| Piece | Process | Sound | Cell |
|---|---|---|---|
| Two Mallets | Phase: part 2 drifts a step ahead over 7 repeats, holds for 4, and so round all 12 offsets | Marimba | 12 steps of D dorian, no rests |
| Wood Shift | Shift: part 2 jumps a step ahead every 6 repeats | Wood block | three pitches and five rests in 12 |
| Glass Steps | Build: the rests fill one at a time, every 2 repeats, then empty; the second part 3 steps behind | Glockenspiel | 7 notes of A pentatonic and 9 rests in 16 |
| Round of Four | Canon: four voices enter 2 repeats apart, 4 steps behind each other, sing together, then leave | Voice ("ooh") | a slow aeolian line in 16 |
| Pulse Field | Pulse: four notes of the cell pulse as a chord on every step, swelling in waves; the chord moves one note along each stage | Electric piano | 8 notes in E flat |
| Long Chord | Stretch: short notes grow longer stage by stage until they overlap into a held chord, then shrink; the second part plays at half speed | Organ | a broken A minor chord and a long rest, in 12 |
| Hidden Lines | Result: two parts phase, and a third picks out the upper notes where they differ, an octave up | Vibraphone | 12 steps of G mixolydian with two rests |
| Tape Bells | Tape: three loops, each a little faster than the last, slide past each other and never lock | Bells | sparse pentatonic notes in 16 |

## The processes

Time is counted in ticks, 960 to a step, a sixteenth note. Each part's position is a function of the elapsed ticks
alone, never a running sum, so a drift is exact however long the piece plays: ten thousand stages into Two Mallets,
part 2 is ahead by exactly what the arithmetic says (`phase-test` checks this). A part starts a note whenever its
position passes into a new step. A stage is Repeats repeats of the cell.

- **Phase**: each stage holds part 2 locked for a stage, then lets it run faster by one step in Drift × Length steps,
  so it gains exactly one step and locks again. A third and fourth part drift two and three times as fast.
- **Shift**: part 2 jumps Jump steps ahead at the start of every stage, with no drift between.
- **Build**: each stage fills one more of the cell's rests, in a fixed scattered order, with the note before it,
  until the cell is full; then the rests come back one at a time. Further parts play the same, Offset steps behind.
- **Canon**: one part a stage enters, each Gap steps behind the one before; then all play for a few stages, and they
  leave last in, first out.
- **Pulse**: every step is a pulse of a chord made of Parts notes of the cell, moving one note along the cell each
  stage. In each stage a wave rises and falls: at its low point, where the chord changes, only one part sounds and
  the notes are short; at its height all parts sound and the notes are longer. Swell sets how deep the wave goes.
  The FM-1 has no velocity per key, so the swell is made with the number of parts and the length of the notes.
- **Stretch**: every note grows by Growth each stage for eight stages, then shrinks. Part 2 plays the cell at half
  speed, part 3 at a third, with notes longer by as much.
- **Result**: parts 1 and 2 phase as in Phase. While they hold locked a step or more apart, part 3 plays the upper of
  their two notes an octave up wherever they differ and the upper note is in the top half of the cell's range; part
  4 plays the lower ones an octave down. In unison and while drifting they rest.
- **Tape**: part 2 runs faster by one step in Drift × Length steps, all the time; part 3 twice as fast again.

Density drops notes from the cell in a fixed scattered order, always keeping the first step. Span sets the parts'
registers: Unison, Close (parts 3 and 4 an octave up), Open (part 2 up, part 3 down, part 4 up) or Wide (part 4 two
octaves up).

## Timing

The processes are plain C (`Sources/PhaseCore/process.c`): integer arithmetic, no allocation, no Swift, so a
firmware can run them as they are. The player (`player.cc`) runs them on its own thread at real-time priority. It
works out the next notes, sleeps with `mach_wait_until` until the next note on or off is due, and presses the
engine's keys then. `mf1_key_down` and `mf1_key_up` take the engine's lock and post to its command queue, so they are
safe from that thread; the audio thread never sees the player's lock.

- Measured with `mf1-render ... phase-jitter`: notes leave the player on average 9 µs late, 50 µs at worst, over 5
  seconds on the development Mac under light load. That is the thread waking, not the sound.
- The engine applies a note at the start of the next audio buffer, as it does for every key, so the sound's timing
  is the player's plus up to one output buffer.
- A tempo change keeps every part where it is and runs on at the new tempo. A change to how the parts move (process,
  Motion, parts, repeats or length) restarts the process from unison on the next step, so the beat carries on.
  Other changes (sound, gate, density, transpose) take effect from the next note.

The sound is single-timbre, so all parts share one voice. When two parts strike the same note together it is struck
once; a note that two parts, or a part and a held key, are holding is counted, and stops only when the last of them
lets go. Stopping, changing piece, leaving the mode and quitting let go of every note the process holds.

## Controls

- **PLAY/STOP** starts and stops the process. **BEGIN** (the GLO button) starts it again from unison. Space does the
  same as PLAY/STOP, and a click on the score plays or stops it.
- **Keys write the cell**: the first key of a phrase stops the process, so the phrase is heard alone. Each key adds a
  note. 1.5 s after the last key lets go, the phrase becomes the cell, Length is set to it and the process starts.
  The step is a little under the typical time between keys, and a longer pause becomes one to three rests, so the
  cell keeps the phrase's rhythm. While writing, the screen shows the cell it will become and a red bar running out.
  **WRITE** (the REC button) turns this off, so the keys play along over the process instead.
- **PIECE** (the PRESETS knob) picks a piece; `<` and `>` do the same. **PROCESS** (the ALGORITHM knob) changes the
  process.
- **PAGE** (the SELECT knob) moves KNOB1-4 between four pages. PROC, CELL, SOUND and TIME jump straight there.
  - **Process**: Process; Motion, the process's own amount, named for it (Drift, Jump, Offset, Gap, Swell, Growth);
    Parts, 1 to 4; Repeats in each stage.
  - **Cell**: Length, 2 to 16 steps (longer than the cell adds rests); Density; Transpose; Gate, how long each note
    is held.
  - **Sound**: Sound; Bright; Decay; Room (reverb).
  - **Time**: Tempo, four steps to a beat; Echo (delay mix); Echo Steps, the delay's time in steps at the tempo, as
    near as its 0.1 to 0.8 s reach allows; Span.
- **PARTS** (LFO) and **SPAN** (EDIT) step through those. **OCT-** and **OCT+** move the keys and the running piece by
  octaves.
- Hold **RESET** (the SAVE button) to put the piece and its cell back as written.
- Every piece keeps its settings and its cell, in `~/Library/Application Support/Phase/pieces.json`.
- On the screen: click a page to show it, click a value to step it, drag or scroll on it to turn it, drag on the
  score to change the process, and click the title for the list of pieces.

## Honesty about parity

- The sounds use only what a real FM-1 has: DX7 voice parameters (all on algorithm 5, the organ on 32) and its
  reverb and delay. Faithful mode is always on, and Extended mode off.
- The processes are the front end pressing keys, as a firmware would. A real FM-1 has no such scheduler; the modes
  firmware (below) brings one.
- Echo Steps sets the delay's time from MF-1's delay, which is a generic design fitted to the published CC range
  (0.8 s down to 0.1 s). On the hardware the steps may land a little off the beat.
- MIDI notes from outside play straight into the engine and are not counted with the process's notes, so a note-off
  from outside can stop a note a part is holding.
- The sounds were shaped with numbers, not by ear: `mf1-render ... phase-report` prints each sound's level, how long
  it rings and its brightness. The mallets die away by themselves while held, the wood block within a tenth of a
  second; the organ and voice hold until let go. They have not been auditioned on a real FM-1.
- File › Export Sound Bank writes the eight pieces' sounds as an ordinary DX7 bank. The processes and effects are
  not in it.

## On the FM-1

Phase is the fourth mode of the modes firmware (`firmware/fm1-modes`, B on the chooser). Its pieces and sounds are a
C port (`firmware/phase/dsp`), and its processes are this same `process.c`, copied unchanged; that is why the file
needs no C library and makes every 64-bit product in one out-of-line function (the FM-1's CPU can corrupt a register
when a 64-bit multiply sits beside a memory load). `firmware/phase/tests/run_parity.sh` checks every value of every
field gives the desktop's voice bytes and effects, and that the firmware's player starts and releases the same notes
as `player.cc` for every piece, within one engine block, through changes and a stop.

- **Where the player runs.** In the audio interrupt, before each of the engine's 64-sample blocks, as the core's
  arpeggiator does. The screen's loop draws a frame in tens of milliseconds and would delay notes by as much; the
  interrupt is never late to the engine, and the engine starts notes only at block boundaries anyway. The ticks are
  counted from the samples with an exact remainder (64 × tempo ticks a second at 44.1 kHz), so the process keeps the
  desktop's time however long it runs. Everything due within a block is done at its start, releases first at the
  same tick, so a note sounds at most 1.45 ms early, every part alike. On the Mac its cost is 0.5 to 4 % of the
  engine's for four parts; on the FM-1 the 64-bit divisions are software, and the cost there is not measured.
- **Notes.** The player counts its notes with the keys' in the core (`md_hold`, `md_let_go`), so a part letting go
  never cuts a key's note or another part's, as on the desktop. A MIDI note from outside is not counted, as on the
  desktop. The process's notes do not go out over USB MIDI; the keys' do.
- **The panel.** As on the desktop: PLAY/STOP, BEGIN (GLO), WRITE (REC), PARTS (LFO), SPAN (EDIT), < and >
  (ARP, SEQ), the octave buttons, PROC/CELL/SOUND/TIME (HOME, FX, SEL, ENV) and hold RESET (SAVE). PLAY is lit at
  the top of each step of part 1 and dim between; REC is lit while the keys write and blinks while a phrase is
  written. The FM-1 has no screen to click, so there is no list of pieces and no drag on the score.
- **The screen.** The desktop's score drawn with the firmware's fills: rings, arcs and spots, the red line, the
  status, tabs and values, in 80-row bands. It moves at 20 frames a second while playing or writing, and at rest
  only when something changes. The fonts are the firmware's (Barlow), with the desktop's letter spacing.
- **Saving.** The settings and cells of all eight pieces, the page, the octave and whether the keys write are kept
  in a store object of Phase's own. Nothing is saved while the process plays, as a flash write silences the sound
  for a moment: stop it, and the save follows. Settings changed while playing are lost if the FM-1 is switched off
  before a stop.
- Not on the FM-1: the bank export and the bank prompt, as the modes firmware takes no SysEx.

## Tools

- `mf1-render <banks> phase-test`: the Phase checks alone (they also run in `test`).
- `mf1-render <banks> phase-report [note]`: each sound's loudness, how long it stays within 20 and 40 dB, and its
  brightness, for one held note.
- `mf1-render <banks> phase-wav <piece> <out.wav> [seconds]`: a piece's process played through the offline engine.
- `mf1-render <banks> phase-jitter [seconds]`: runs the real player thread and prints how late its notes left.
- `MF-1 --phase --snapshot out.png [screen|sheet] [piece N] [page N] [process N] [at STEPS] [write 1] [stopped 1]`:
  renders the panel, the screen at four times the size, or every piece's screen on one sheet. Without `at` it draws
  a moment where the process is well under way.
- `MF-1 --phase --selfcheck`: drives the model as the panel would, muted, and checks the engine and the player follow.
