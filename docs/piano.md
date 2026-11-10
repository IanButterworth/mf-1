# Piano

Piano is a grand piano made from measurements of a real one, for MF-1 and for the FM-1 itself. View › Piano Mode
switches to it (`--piano` starts in it); on the FM-1, hold C while switching on. Like the other modes it has its own
state folder and MIDI port ("Piano (FM-1 Compatible)").

Unlike the other modes, Piano does not use the DX7 engine. A DX7 voice has six operators, too few for a piano's
dozens of inharmonic partials with their own decays. Piano has an engine of its own (`Sources/PianoCore/piano.c`):
plain C, single-precision, no C library and no allocation, so the FM-1's firmware builds the same file and renders the
same samples. The effects (the room is MF-1's reverb) and MASTER follow it as they follow the DX7 voices.

## Where the sound comes from

Every third key of a Steinway model B (A0 to C8), recorded at pp, mf and ff by the University of Iowa Electronic
Music Studios (Musical Instrument Samples, free for any use). The recordings are measured only; none is in the
repository, and the engine plays no samples. `Sources/PianoCore/tools/` holds the scripts:

- `analyze.py` measures each note: the inharmonicity B and tuning from the partials' frequencies
  (f_n = n f0 √(1 + B n²)); for every partial, its level and a two-stage decay fitted to its envelope (the fast and
  slow decays of a key's coupled strings), and the beat between them; the peaks off the harmonic series in the bass
  (longitudinal modes and phantom partials); and the energy between the partials in the first 200 ms (the knock).
- `gen_tables.py` turns the measurements into `piano_tables.inc`: inharmonicity and stretch as smooth curves over the
  keyboard, every partial of every measured key, and how much quieter and darker mf and pp are than ff.
- `calib.py` closes the loop: it renders every measured key, compares each partial with the recording at the strike
  (8 to 40 ms), at 0.15 to 0.5 s and at 1.2 to 3 s, wherever the recording is clear of its noise, and corrects that
  partial's level, fast decay and slow stage. Its corrections are `calib.json`, which `gen_tables.py` applies.
- `board_cal.py` fits the soundboard's modes to the knock, band by band. `sweep.py` compares the whole keyboard in
  third-octave bands; `onset.py`, `rise.py` and `decaycmp.py` look at the first 150 ms and at each partial over time.

## The engine

- **Strings.** A struck key is up to 128 partials at their measured frequencies, each two decaying oscillators started
  together: a fast one and a slow one a beat apart. Together they make the two-stage decay and the slow beating of a
  real piano's unison strings. An oscillator is a complex number turned and shrunk once a sample: four multiplies.
  Keys between the measured ones interpolate partial by partial. In the bass, the peaks off the harmonic series are
  added too, growing twice as fast as the partials with velocity, since they come from the string's stretching.
- **Velocity.** pp at velocity 24, mf at 64, ff at 110, each partial's level interpolated between the measurements, and
  carried on beyond them. A softer blow loses its top far faster than the low partials show: 12 dB an octave above
  1.5 kHz at mf, fitted to the recordings.
- **The rise.** A partial grows while the hammer is on the string: with a time constant of about 8.5 ms up to 300 Hz,
  falling to 2 ms by 2 kHz, as measured at ff, and slower from a softer blow. An oscillator of the opposite sign,
  dying at that rate, cancels each partial at the strike and lets it through as it fades. Without it the partials
  above 1.5 kHz started 12 to 30 dB too loud, a hard wooden click.
- **The knock.** The hammer's blow, through the key and the bridge, rings the soundboard. One board of 32 resonant
  modes, 55 Hz to 2.4 kHz, serves every note. Each strike is a burst of noise as long as the hammer touches the
  string (2 ms at ff, 4 ms at pp), which each key feeds to the modes in its own proportions. The modes' levels are
  fitted to the recordings' knock, within about 1 dB a third-octave band from 80 Hz to 2.5 kHz.
- **Dampers and pedal.** A key's damper lifts while it is held or the pedal is down; released, the strings die in
  0.8 s (bass) to 0.3 s (treble). Above F6 there are no dampers. The damping times are inferred, not measured: the
  recordings hold every note to its end.
- **The output.** A limiter holds peaks at 0.85 of full scale, acting at once on a peak and recovering over 150 ms; a
  single note never reaches it, only a dense loud chord. On the FM-1, while its own speaker plays, a 160 Hz high-pass
  (12 dB an octave) keeps the bass's fundamentals from driving the small speaker into distortion; with headphones in
  the full bass comes back. The app never applies it. Volume 70 puts a C4 at ff at the level of the FM-1's own piano
  preset.
- **The pool.** At most 384 oscillators sound, in the app and on the FM-1 alike. A note takes its loudest partials
  first; when the pool is full, a new partial replaces the quietest sounding one, if it is louder. Partials stop at
  about -110 dB. On the FM-1 the budget also follows the audio interrupt's load (it drops above 80% and recovers below
  50%), so a heavy pedalled chord loses its quietest partials rather than breaking up.

How close it comes, against the recordings at ff: each measured partial within 2.7 dB rms at the strike, 5 dB from
0.15 to 0.5 s and 3.1 dB from 1.2 to 3 s; the first 20 ms within about 2 dB below 1.5 kHz and 4.5 dB above. Across
third-octave bands and four windows of time, about 9 dB rms at ff and at mf. Some of the difference is the recording, not the piano:
its A0 is tuned 72 cents sharp, and its room and noise fill the bands below each note.

## The pianos

Voicings of the one measured piano, each a change a technician, a room or a player's taste would make.

| Piano | What changes |
|---|---|
| Concert Grand | nothing: the piano as measured, in a hall |
| Bright Grand | harder hammers, more knock |
| Mellow Grand | softer hammers |
| Felt Piano | a felt strip before the hammers: dark, short, little knock |
| Studio Dry | close microphones, no room |
| Concert Hall | heard from the stalls |
| Honky-Tonk | unisons tuned far apart, little stretch, short sustain |
| Old Upright | short strings: shorter sustain, wider unisons, more knock |

## Controls

| Control | Does |
|---|---|
| PIANO (PRESETS), < and > | the voicing |
| TOUCH (ALGORITHM) | how hard the panel's keys play, 1 to 127. Notes from MIDI play at their own velocity |
| PAGE (SELECT), TONE, STRING | KNOB1-4's page |
| Tone page | Tone (the hammer, soft to hard), Sustain, Room, Volume |
| String page | Unison (the detune between a key's strings), Knock, Stretch (of the octaves), Damper (how fast) |
| PEDAL (GLO) | the sustain pedal, down until pressed again. MIDI controller 64 too |
| SPACE (SEL) | the room: room, hall or plate |
| RESET (hold SAVE) | the piano as voiced |

The screen shows the open grand from above: its strings brighten and sway as they ring, and the dampers lift off them
while a key or the pedal holds them.

## Known differences and open questions

- The FM-1's keys sense no velocity in MF-1's firmware, so on the device TOUCH sets it. fm1-base can read velocity
  from the keys (`APP_TOUCH`); using it is the next step for the device.
- The stock FM-1's 5.6 kHz roll-off and velocity scaling belong to M-VAVE's firmware path, so Piano switches the
  emulator's FM-1 V15 behaviour off. If the roll-off turns out to be on the board's analog output, the device sounds
  darker than the app.
- Release times, sympathetic resonance with the pedal down, and the damper's own noise are not measured. Sympathetic
  resonance is not modelled yet.
- Piano has run on one FM-1 (2026-11-10): it boots, plays and logged no crash. The first build was quiet and its bass
  distorted the speaker, which the louder level and the speaker's high-pass address. The CPU load at 384 oscillators
  has not been measured; the budget is an estimate, guarded by the load-following budget.
- The soundboard rings for 100 ms or more. Above 1.5 kHz the recordings' strike looks more like a 2 ms click than a
  ring, so the board's ring may be too long; not yet changed.
