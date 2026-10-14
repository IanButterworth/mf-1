# M-VAVE FM-1: hardware UI and feature research

Research date 2026-10-14. Stock firmware V15 (2026-07-30) unless noted.
Tags: **[M]** = official "Detailed User Manual" PDF (revision 2026-8-10, in `ref/FM-1-manual.pdf`),
**[RN]** = official firmware release notes (`ref/FM-1_release_notes.txt`), **[MIDI]** = official
MIDI table (`ref/FM-1_MIDI_EN.txt`), **[P]** = official product data (`ref/products-data.js`),
**[PH]** = my measurement/observation from photos (approximate), **(?)** = uncertain / inferred.

## Sources

| Key | URL |
|---|---|
| Manual PDF (official, EN+CN, 31 pp) | https://manualf.oss-cn-hongkong.aliyuncs.com/manual/MIDI-KEYBOARD/FM-1-manual.pdf |
| Manual PDF (Amazon-hosted, multi-language printed manual, 109 pp, not read in full) | https://m.media-amazon.com/images/I/A1WOydif9HL.pdf |
| Product page (JS rendered) | http://www.cuvave.com/product?id=fm-1 (data in http://www.cuvave.com/products-data.js) |
| Firmware release notes | https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1.txt |
| MIDI implementation (EN docx) | https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/releaseNote/firmware/FM-1%20MIDI%20EN.docx |
| Synth Anatomy review | https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html |
| Synthtopia | https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/ |
| Sonicstate | https://sonicstate.com/news/2026/07/10/m-vave-fm-1-reviews-/ |
| Tao of Mac | https://taoofmac.com/space/reviews/2026/10/11/2100 |
| Elektronauts | https://www.elektronauts.com/t/m-vave-fm-1/252170 (and `?page=2`) |
| awesome-fm-1 (resource index) | https://github.com/cicloid/awesome-fm-1 |
| MvaveFM1-Designer (screen/LED facts) | https://github.com/Quixotic7/MvaveFM1-Designer |
| Factory banks (.syx) | https://github.com/KingParamount/fm1-factory-presets |

Geeky-gadgets and bonedo: not fetched (no unique UI facts expected beyond the above; not verified).
YouTube: only the Synth Anatomy review (https://www.youtube.com/watch?v=q3e2zH_-5I8) and Maks Makes
CC video (https://www.youtube.com/watch?v=vWRd1A8I3gc) descriptions were retrievable; neither has UI
details. Official video https://www.youtube.com/watch?v=rTbvNZyP5wM returned no description.

---

## 1. Physical layout

### 1.1 Overall

- Size 161.5 (L) x 96.5 (W) x 28.6 (H) mm, 251 g [M p.14] [P]. Aspect ratio 1.674:1.
- Rounded-rectangle case, corner radius about 9 mm [PH]. Flat top panel; knobs protrude.
- Knobs: "1 x 300° Volume Knob, 7 x 360° Control Knobs" [M p.14]. The 7 are endless encoders
  with **no push switch** (MvaveFM1-Designer). MASTER is a pot with a white pointer dot [PH].
  The encoders are black, knurled, with no pointer [PH].
- Keys: "27-key silicone keybed" [M]. Each key is an oval/stadium-shaped rubber pad with a vertical
  LED slit (light pipe) in its centre [PH]. Keys feel "very soft and rubbery" (Elektronauts).
- Display: 1.54" TFT [M p.2], 240x240 px (MvaveFM1-Designer), colour.
- Built-in speaker on the **underside** (grille is a round "spiral spoke" pattern near one corner)
  [M p.13] [PH fm-1-5]; auto-mutes with headphones [M].
- Rear edge (behind MASTER/SELECT/screen), left to right as seen **from the rear** [M p.14] [PH fm-1-4]:
  ① 3.5 mm TRS audio out (in a recessed rounded-rect plate), ② 3.5 mm TRS MIDI IN,
  ③ USB-C, ④ slide power switch, then a tiny LED dot (charge/power indicator?) (?).
  The rear face also has a large "M-VAVE" logo, behind the KNOB1-4 area [PH].
  Seen from the front/top, the port order is mirrored: LED, power, USB-C, MIDI, audio from left to right (?).
- Four round rubber feet on the bottom; a rectangular plate in the middle of the underside [PH].

### 1.2 Colourways and hex estimates

Official swatches from products-data.js [P]: **Black `#1c1c1e`**, **Purple
`linear-gradient(135deg, #e6b8e0 0%, #8a5cc4 100%)`**, **Orange `#f15a24`**. Taoofmac also shows a
black/green "DX7 livery" unit, which is a skin or variant, not an official colourway (?).

Sampled from the official top-down renders [PH] (lit renders; real colours are darker):

| Element | Black | Purple | Orange |
|---|---|---|---|
| Top panel (case) | render #5f5f5f-#353535; real ≈ #2a2a2c (matte charcoal-black) | #c19ad5 / #b98fcb (lilac) | #c4744d / #b96848 |
| Keybed recess panel | ≈ #383838 render, real ≈ #1f1f21 | #695c9e (violet) | similar darker tone (?) |
| Keys (silicone) | #2b2b2b-#333333 | #685da9 | #7d453b |
| Function-button panel | #393939 | #675ea2 | (?) |
| Function buttons | #252725 | #7a6fb3 | #8e574a |
| Knobs | black #1e1e1e-#4d4d4d | black #212121 | black #1c1d1d |
| Screen bezel (glass border) | #0b0e0d (near black) | #0c071e | black |
| Panel legends (MASTER, KNOB1...) | light grey/white ≈ #d8d8d8 | white | white |
| Key LED slit, unlit | ≈ #3e3e3e grey | #373745 | dark |
| Key LED slit, lit | white / cool white (photos ≈ #a7f0fd, i.e. bluish white) | | |

LED colours [M] [PH]: key LEDs are white (MvaveFM1-Designer: "one white LED" each). Function-button
legends are **back-lit text**: unlit legends are dim grey-blue, active ones bright white [PH,
synthanatomy photos]. REC lights red; PLAY lights green; STOP lights orange (PLAY/STOP is one
button with a bicolour LED) [M p.10-11]. The photo of a stopped SEQ shows the PLAY/STOP legend
glowing red-orange [PH]. Elektronauts user GHRS: the 6x2 grid is unlit until a button is selected.

### 1.3 Panel fonts [PH]

- Printed panel labels (MASTER, SELECT, PRESETS, ALGORITHM, KNOB1-KNOB4): thin, square, pixel/LCD-style
  monospace caps (like a 5x7 dot-matrix font, e.g. "VT323"/"Share Tech Mono" would approximate).
- Function-button legends (FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY/STOP, REC) and
  OCT-/OCT+: bold, squarish "techno" sans with a stylised E (Orbitron/Eurostile-like). PLAY over STOP
  with a horizontal rule between them.
- Key labels (OP1...POLY): small plain sans (Arial-like), printed under the LED slit of the upper-row keys.

### 1.4 Labels exactly as printed

Left block: `MASTER`, `SELECT` (top row); `PRESETS`, `ALGORITHM` (second row); buttons `OCT-`, `OCT+`.
Right block: `KNOB1`, `KNOB2`, `KNOB3`, `KNOB4` (no space). Button grid:

```
row 1:  FX    SEL   ENV   LFO   EDIT   GLO
row 2:  HOME  SAVE  ARP   SEQ   PLAY/STOP  REC
```
Keybed upper-row labels: `OP1 OP2 OP3 | OP4 OP5 | OP6 PIT GLO | MONO POLY | (blank)`.

### 1.5 Coordinates (from the top-down render fm-1-1, 5.2 px/mm; origin = top-left of case) [PH]

All values are mm, accurate to about ±1 mm.

| Element | Centre x | Centre y | Size |
|---|---|---|---|
| MASTER pot | 12.5 | 14.4 | Ø ≈ 7 (knob cap) |
| SELECT enc | 29.2 | 14.4 | Ø ≈ 7 |
| PRESETS enc | 12.5 | 31.3 | Ø ≈ 7 |
| ALGORITHM enc | 30.2 | 31.3 | Ø ≈ 7 |
| Labels row 1 / row 2 baseline | | 7.3 / 24.2 | caps height ≈ 2 |
| OCT-/OCT+ tray | 23.3 | 45.3 | 25.6 x 7.7, OCT- at x 15.8, OCT+ at x 28.1, each ≈ 10.6 x 5.8 |
| Screen bezel (black, rounded r≈4) | 38.1 | 36.5 | 38.5 x 38.5 |
| Screen active area | 38 | 36.5 | ≈ 28.5 x 28.5 |
| KNOB1..KNOB4 | 91.0 / 109.2 / 127.3 / 145.8 | 14.4 | Ø ≈ 7, labels at y ≈ 7.3 |
| Button tray (raised rounded rect) | condensed | | x 86-151, y 26.5-48.5 |
| Function buttons, cols 1-6 | 93.3 / 103.5 / 113.5 / 123.7 / 133.8 / 144.0 | row1 32.7, row2 42.7 | ≈ 8.5 x 8.5, r ≈ 1.5 |
| Small dot (LED or mic?) in grid centre between ENV/LFO and ARP/SEQ (?) | 118.6 | 37.7 | Ø 0.6 |
| Keybed recess (rounded rect r≈6) | 80.8 | 75.5 | 151 x 35.6 (x 5.2-156.3, y 57.7-93.3) |
| White-key row (16 keys) | x = 12.5 + 9.13*i (i=0..15) | 80.8 | each ≈ 8 x 16 |
| Black-key row (11 keys) | see below | 63.8 | each ≈ 8 x 14 |
| Tiny dots between black groups (B/C and E/F gaps) | 44.4, 71.5, 108.1, 135.4 | 72.7 | Ø 0.6 |

### 1.6 Keyboard [M p.12] [PH]

- 27 keys = **16 "white" (lower row) + 11 "black" (upper row)**, all same shape/colour (silicone pads);
  only the row position distinguishes them.
- MIDI note = key position + 53 + octave*12 + transpose. Default range **F3 (53) to G5 (79)** [M].
- Lower row, left to right: F3 G3 A3 B3 C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5.
- Upper row with secondary labels (active in EDIT mode only):

| Note | F#3 | G#3 | A#3 | C#4 | D#4 | F#4 | G#4 | A#4 | C#5 | D#5 | F#5 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Label | OP1 | OP2 | OP3 | OP4 | OP5 | OP6 | PIT | GLO | MONO | POLY | (none) |
| Centre x (mm) | 17.1 | 26.2 | 35.1 | 53.6 | 62.7 | 80.8 | 89.8 | 98.8 | 117.3 | 126.3 | 144.6 |

  Gaps in the upper row at B/C and E/F, with a tiny dot in each gap (decorative or LED (?)).
- Each key has an LED that lights when pressed [M]. Keys are velocity-sensitive? **Unclear.**
  The GLO "Velocity (1-127)" global setting suggests the keyboard sends a fixed velocity (?).
- Octave: OCT- / OCT+ one octave per press, range -3..+3 [M]. Transpose: hold OCT-/OCT+ and turn
  ALGORITHM, -12..+12 [M]. Press both: reset octave and transpose to 0 [M]. Product page claims
  "OCT-/OCT+ over 8 octaves" [P].
- Octave LED (on OCT- side if negative, OCT+ side if positive) [M p.2]:
  0 = both off, ±1 = slow blink, ±2 = fast blink, ±3 = solid. Whether this is the OCT button
  backlight or a separate LED is not clear from photos (?) (probably the button legend backlight).

### 1.7 ASCII diagram (to scale, 1 char ≈ 2 mm horizontally, ≈ 4 mm vertically)

```
 .--------------------------------------------------------------------------------.
 |  MASTER   SELECT  .------------------.  KNOB1    KNOB2    KNOB3    KNOB4         |
 |   (o)      (@)    | 001 PIANO 1  [II]|   (@)      (@)      (@)      (@)          |
 |                   |------------------|                                         |
 |  PRESETS ALGORITHM|                  | .-------------------------------------. |
 |   (@)      (@)    |   centre area    | | FX | SEL | ENV | LFO | EDIT | GLO   | |
 |                   |                  | |    |     |     .     |      |       | |
 | .-------------.   |------------------| |HOME|SAVE | ARP | SEQ |PLAY/ | REC   | |
 | |[OCT-] [OCT+]|   |  ( 1/2 LFO )     | |    |     |     |     |STOP  |       | |
 | '-------------'   '------------------' '-------------------------------------' |
 | .----------------------------------------------------------------------------. |
 | |  [OP1][OP2][OP3]   [OP4][OP5]   [OP6][PIT][GLO]   [MONO][POLY]    [  ]    | |
 | |               .              .                 .               .          | |
 | | [F ][G ][A ][B ][C ][D ][E ][F ][G ][A ][B ][C ][D ][E ][F ][G ]          | |
 | '----------------------------------------------------------------------------' |
 '--------------------------------------------------------------------------------'
   rear edge (top): power LED, power switch, USB-C, MIDI IN (TRS), audio out (TRS)  (?)
```

Relative proportions (fraction of width / height): left knob block 0-0.23; screen 0.20-0.47 (x),
0.17-0.57 (y); KNOB row at y 0.15; button grid 0.53-0.94 (x), 0.27-0.50 (y); keybed 0.03-0.97 (x),
0.60-0.97 (y).

---

## 2. Screen UI

### 2.1 General [M p.2-3] [PH]

- 240x240 colour TFT. Three areas:
  - **Top bar**: current voice number (3 digits) + name, e.g. `001 PIANO 1`, `032 BRASS 1`;
    battery icon (rounded-rect outline with 1-3 vertical bars) top-right. A thin separator line below.
  - **Centre area**: mode-specific content.
  - **Bottom bar**: a rounded "pill" button-like label showing current mode name and page, e.g.
    `1/2 LFO`, `1/2 Arpeggio`, `1/2 Sequencer`, `ENV`, `OSC` (home); when a knob is turned it shows the
    exact value, e.g. `CutOff: 1004Hz`, `Mix: 70%`, `Type: Low Pass`, `Freq: 30%` [M] [PH].
- Look [PH]: light UI theme. Pale blue-grey background (render ≈ #bdd5dd top, gradient), parameter
  boxes are rounded rectangles (r ≈ 6 px) with a subtle vertical gradient, darker blue-grey border and
  drop shadow (render boxes ≈ #748391-#5b7185 when OFF/dim); dark text (≈ #1a1a1a). Photos of a real
  unit look much more saturated cyan (#5fbcff background, #51cffe boxes), which is likely camera
  exaggeration plus a cool-tinted panel (?). Sans-serif font for text (humanist, similar to
  Noto Sans / DejaVu Sans), ≈ 18-20 px for top bar, ≈ 16-18 px in boxes.
- Home screen ("OSC"): darker blue-teal background (≈ #2f5264-#315066), horizontal centre trace line
  across the full width (flat when silent), `OSC` pill at bottom. Manual: "a yellow line on a gray
  background that moves with the sound you play" [M p.8]. The render shows the line yellowish-grey.
- Parameter pages with 4 knob-mapped params use a **2x2 grid** of boxes (box name on line 1, value on
  line 2), mapped KNOB1=top-left, KNOB2=top-right, KNOB3=bottom-left, KNOB4=bottom-right [PH: ARP
  page shows `Up` | `Octave 1` / `Rate 1/16` | `Tempo 120`; SEQ shows `Pattern 1` | `Clear` / `Rate 1/16`
  | `Tempo 106`; ENV shows `Attack` | `Decay` / `Sustain` | `Release`]. In ENV mode values are drawn as
  bar graphs ("more blocks = higher value") [M]. Note that on the ARP page, the mode box shows only the
  mode name (`Up`).
- Popups (overlay centre, vanish after about 1 s) [M p.2-3]: voice list while turning PRESETS
  (number + name rows, current highlighted); algorithm diagram while turning ALGORITHM; `Octave` + value;
  `Transpose` + value; `Volume` + value; `Preset Saved`; `LOW BATTERY` (full screen, persistent until
  recharged). Other messages: `Disable SEQ to enter`, `Disable ARP to enter`, `Enable SEQ first`,
  `Reset Success` [M].
- Algorithm diagram popup [PH synthanatomy DX-7 photo]: algorithm number in a rounded pill top-left
  (e.g. `08`), operators as small rounded squares numbered 1-6 (cyan fill, dark border), connected by
  lines, feedback loop drawn as a bracket line on the feedback operator, carriers sit on a bottom
  `OUT` pill. Background remains the light theme.
- FX page: 3x2 grid of boxes `Filter Reverb Delay / Distortion Chorus Phaser` (in current chain order;
  "Distortion" wraps as `Distort` / `ion`). Bright box = effect ON, dim box = OFF. A **red dot**
  (≈ #e04133) under the selected slot, blinking; solid while grabbed with SEL [M p.6] [PH].

### 2.2 Navigation model [M]

- Mode buttons are toggles: press FX / LFO / EDIT / GLO / ENV / ARP / SEQ to enter; press again to
  return to HOME. HOME returns to the oscilloscope at any time. The button LED of the current mode is lit.
- SELECT turns pages within a mode (clockwise = next page, counter-clockwise = previous); in FX it
  moves between the 6 slots; in EDIT it pages the selected operator section (up to 6 pages).
- KNOB1-4 edit the 4 parameters shown. Turning a knob shows the value in the bottom pill.
- PRESETS loads voices 001-128 immediately, wrapping around. It is inactive while SEQ is on [M p.11].
- ALGORITHM changes the current voice's algorithm 1-32; with OCT-/OCT+ held, it sets transpose.
- No SHIFT button. "Shift" combos are: hold OCT± + ALGORITHM (transpose), OCT- + OCT+ (reset),
  long-press HOME (Bluetooth on/off), long-press OPx key in EDIT (mute operator), hold SAVE (save),
  hold REC + turn SELECT (step navigation), hold key + turn SELECT (long note) [M] [RN].
- Several "actions" are triggered by rotating a knob clockwise (enter) / counter-clockwise (cancel),
  e.g. Init Voice, Rename, Copy Voice, Reset Patches, SEQ Clear (?), FX on/off [M].

---

## 3. Feature reference

### 3.1 Presets [M] [P]

- 128 voices, 001-128, in four 32-voice banks A (1-32), B (33-64), C (65-96), D (97-128)
  (reader comment on Synth Anatomy; consistent with manual SysEx import to "bank A/B/C/D").
- Voices are DX7-format (standard 32-voice VMEM SysEx import; choose bank A-D on receipt) [M p.9, 13].
- Factory names (from the community-extracted factory .syx in `ref/banks/`; firmware updates have
  changed some voices, e.g. a photo shows `006 STONED DX`, another `001 PIANO 3`, so treat as
  approximate (?)):
  - A: 001 PIANO 1, 002 ORGAN 1, 003 SYN LEAD 1, 004 SYN PAD 1, then repeating PIANO n / ORGAN n /
    SYN LEAD n / SYN PAD n for n=2..8 (028 SYN PAD 7, 032 SYN PAD 8).
  - B: GUITAR n / DS GUITARn / BASS n / SYN BASSn, n=1..8.
  - C: BRASS n / WOODWIND n / STRING n / VOICE n, n=1..8.
  - D: 097 BOWOAN, BDWHOCARES, BLOCK, WAVES, FROGBELL, ELECT.GONG, TORIMTORA2, MONO M, TIBET,
    LOG DRUM, COWBELL, MONO Z, FINGERCYM, TIMPANI, TAMBOURINE, DESCENT, CLANG BELL, STEEL DRUM, CONGA,
    SAW EM UP, BI   BEN, KALIMBA, GAMALONG, SAW EM UP2, TUB BELLS, BRUSHES, TOM TOMS, SAW EM UP3,
    ECHO, TIMBALI, PERCUSSION, 128 WHAT IF.
- Effects settings are saved per patch (Synth Anatomy).
- Program Change 0-127 selects voice 001-128 [MIDI] [RN V13].

### 3.2 Save / init / rename / copy / reset [M p.5, 7-9] [RN V15]

- **SAVE**: press and hold. Saves pending changes (voice params, FX, sequencer patterns, global
  settings) to flash. SAVE LED on while saving; screen shows `Preset Saved`; LED off on release.
  There is no "save to another slot"; it overwrites the current voice (Copy Voice is the way to
  duplicate).
- **Init Voice** (EDIT > GLO page 2, KNOB1 clockwise): resets all params to a basic template.
- **Rename** (EDIT > GLO page 2, KNOB2 clockwise opens rename panel): OP1 = A-Z, OP2 = a-z,
  OP3 = symbols, OP4 = numbers, OP5 = delete [RN wording: "op1: A-Z op1: a-z op1: symbol op1: number
  op5: delete", the manual says "OP1-OP5 for character selection/deletion"]; KNOB3 picks the character;
  SELECT confirms; KNOB2 counter-clockwise cancels. Name length 10 chars (DX7 format) (?).
- **Copy Voice** (EDIT > GLO page 2, KNOB3 clockwise opens; counter-clockwise exits): KNOB4 selects the
  source voice; SELECT confirms overwriting the current voice (RN: "rotate select to confirm";
  the encoders have no push, so "press SELECT" in the manual means turn).
- **Reset Patches** (GLO page 2, KNOB1 clockwise): KNOB1-4 choose bank A/B/C/D; SELECT clockwise
  confirms, counter-clockwise cancels; shows `Reset Success`. Restores that bank to the **last imported**
  voices.

### 3.3 Voice editing: EDIT mode [M p.4-5, 7-8, 12]

Press EDIT. The upper-row keys become selectors: all 8 OP/PIT/GLO key LEDs blink, the selected one is
solid; MONO/POLY: active one solid, other blinks. Keys do not play in EDIT (?) (likely; not stated).
The unit remembers the last page for each operator [M] [RN V13]. Long-press OPx mutes that operator
until the voice changes. SELECT pages; KNOB1-4 edit.

| Section | Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
|---|---|---|---|---|---|
| OP1-OP6 | 1 Tuning & Level | Detune (-7..7) | FreqCoarse (0-31) | FreqFine (0-99) | Out Level (0-99) |
| | 2 Envelope | Rate 1 (0-99) | Rate 2 | Rate 3 | Rate 4 |
| | 3 Envelope | Level 1 (0-99) | Level 2 | Level 3 | Level 4 |
| | 4 Sensitivity & Mode | A ModSens (0-3) | KeyVelocity (0-7) | OscMode (Ratio/Fixed) | - |
| | 5 Keyboard Scaling | L Depth (0-99) | R Depth (0-99) | BreakPoint (A-1..C7) | - |
| | 6 Keyboard Scaling | L Curve (-LN/-EX/+LN/+EX) | R Curve (same) | RateScale (0-7) | - |
| PIT (pitch EG) | 1 | Rate 1-4 (0-99) | | | |
| | 2 | Level 1-4 (0-99) | | | |
| GLO (voice global) | 1 | Algorithm (1-32) | Feedback (0-7) | Osc Sync (On/Off) | Transpose (0-48) |
| | 2 | Init Voice | Rename | Copy Voice | - |

MONO key = mono, last-note priority. POLY key = up to 12 voices.

### 3.4 LFO [M p.4, 7]

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
|---|---|---|---|---|
| 1 | Wave (Triangle, Saw Down, Saw Up, Square, Sine, Sample & Hold) | Speed (0-99) | PMD (0-99) | AMD (0-99) |
| 2 | Delay (0-99) | Pitch Sens (0-7) | Lfo Sync (On/Off) | Osc Sync (On/Off) |

### 3.5 ENV (global amp ADSR overlay) [M p.4, 6] [RN V09, V15]

Disabled by default. Pressing ENV turns the function on (LED lit) and shows Attack/Decay/Sustain/
Release, 0-100 % each (KNOB1-4), drawn as bar graphs. Pressing ENV again turns the envelope function
**off** (so ENV is an on/off toggle as well as a page). It overrides the DX operator EGs (?).

### 3.6 Effects: FX mode [M p.3-4, 6] [MIDI]

Six-slot serial chain, default order Filter, Reverb, Delay, Distortion, Chorus, Phaser; the chain is
mono (Synth Anatomy). SELECT moves the dot; **SEL** grabs the slot (dot solid, SEL LED on); SELECT then
swaps it with its neighbours; SEL again drops it. KNOB4 clockwise = ON, counter-clockwise = OFF.

| Effect | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
|---|---|---|---|---|
| Filter | Type: Low Pass / Band Pass / High Pass | CutOff 100 Hz-20,000 Hz (100 log steps) | Q 0-100 | On/Off |
| Reverb | Type: Room / Hall / Plate | Decay 0-100 % | Mix 0-100 % | On/Off |
| Delay | Decay 0-100 % | Rate 0-100 % | Mix 0-100 % | On/Off |
| Distortion | Gain 0-100 % | Tone 0-100 % | Level 0-100 % | On/Off |
| Chorus | Freq 0-100 % | Depth 0-100 % | Mix 0-100 % | On/Off |
| Phaser | Freq 0-100 % | Depth 0-100 % | Mix 0-100 % | On/Off |

(V15 changed FX/ENV resolution from 0-10 to 0-100 [RN].) MIDI CC (FX channel, default 2) range
differs: Filter cutoff CC2 0-107, Q CC3 0-10 [MIDI], so treat the manual's 0-100 Q as display scale (?).

### 3.7 Arpeggiator [M p.5, 9] [P]

Press ARP: ARP LED on, ARP page shown, arp active. Hold keys; key LEDs light as notes trigger.
Press ARP again in ARP mode: arp stops, LED off, back to HOME. Blocked with `Disable SEQ to enter`
if SEQ is on.

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
|---|---|---|---|---|
| 1 | Mode | Octave (1-4) | Rate | Tempo (30-300 BPM) |
| 2 | Gate (20-100 %) | Swing (50-75 %) | Latch (On/Off) | Sync (On/Off) |

Modes, as in the manual: **Up, Down, Inclusive, Exclusive, Random, Order, Repeat**. The product page
lists "Up / Down / Inclusive / Exclusive / Bidirectional / Random / Order" instead, so the manual
(newer) replaces Bidirectional with Repeat.
- Up: low to high, repeat. Down: high to low. Inclusive: low-high-low including endpoints.
  Exclusive: bouncing, endpoints not repeated. Random. Order: order keys were pressed.
  Repeat: each note repeated before moving on.
- Rate values: musical divisions, e.g. `1/16`; V13 added `1/1` and `1/2` [RN]. Full list not
  documented (?) (likely 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, possibly triplets).
- Latch: notes keep playing after release; new keys replace the pattern. Sync: follow external
  MIDI clock (F8; FA start; FB continue; FC stop) [MIDI].

### 3.8 Sequencer [M p.5, 10-11] [RN]

- Press SEQ: SEQ LED on, STOP LED orange, SEQ page shown. The 16 lower-row (white) keys become step
  indicators: solid = note on step, off = empty, flashing = currently playing (?: the manual says
  "keyboard keys"; the photo of SEQ mode shows all 16 white-key LEDs lit). `Disable ARP to enter` if
  ARP is on. Press SEQ again: stop, LEDs off, back to HOME.
- **PLAY/STOP**: stopped → play (PLAY LED green); playing/recording → stop (STOP LED orange). Shows
  `Enable SEQ first` if SEQ is off.
- **REC**: stopped/playing → record (REC LED red); recording → exit record (REC off, STOP stays lit).
  `Enable SEQ first` if SEQ is off.
- Step recording: each note played (polyphonic chords allowed, Synth Anatomy) goes into the next step.
  Rate can be changed per step while recording (KNOB2 page 2) [M] [RN V09].
- Long notes: on the active step, hold the note and turn SELECT to the target step, then release; does
  not overwrite existing notes [M] [RN V15].
- Rests / replace: in record, long-hold REC and turn SELECT to move to a step; hold keys to add or
  replace notes, or leave it empty for a rest [M].
- 16 patterns; each pattern binds a Voice (PRESETS inactive in SEQ); auto-saved to flash [M].

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
|---|---|---|---|---|
| 1 | Pattern (1-16) | Clear | Chain (1-16) | Step (1-16) (pattern length) |
| 2 | Voice (1-128) | Rate | Tempo (30-300 BPM) | Gate (20-100 %) |
| 3 | Swing (50-75 %) | Sync (On/Off) | Transpose (-24..+24) | - |

Chain = number of consecutive patterns played from the current one. ARP and SEQ share BPM since V13 [RN].
Clear: probably rotate KNOB2 clockwise to clear the current pattern (?) (not documented).

### 3.9 GLO (device global settings) [M p.5, 8]

Note: the **GLO button** in the 6x2 grid = global device settings; the **GLO key** (A#4) in EDIT mode
= voice-global params. Both are spelled `GLO`; the manual also calls the device page "Globe".

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
|---|---|---|---|---|
| 1 MIDI & Pitch | MIDI Ch (ALL / 1-16; default ALL) | Effect Ch (1-16; default 2) | Pitch Bend Up (1-24) | Pitch Bend Down (1-24) |
| 2 System | Reset Patches | Velocity (1-127) | Glide (Full Time / Fingered) | Glide Val (0-100 = 0-1 s) |

- Glide only works in MONO. Full Time: glide on every note. Fingered: only legato (previous key held).
- Velocity 1-127: "overall keyboard velocity response" [M]; the RN calls it "glob strength options
  1-127" (probably the fixed velocity of the on-board keys) (?).
- Transpose (OCT± + ALGORITHM) is saved to global settings automatically.

### 3.10 HOME / Bluetooth [M p.8]

- Short press: Home/oscilloscope. Long press: toggle Bluetooth. HOME LED blinking = BT on, not
  connected; solid = connected; off = BT off.

### 3.11 MIDI [M p.13] [MIDI] [RN]

- USB MIDI (class compliant), BLE MIDI, 3.5 mm TRS MIDI IN (Type A, per a Synth Anatomy reader); all
  usable at once. Also USB audio (Elektronauts, Tao of Mac; not in the official specs).
- Receives on Note channel (midich, default ALL): Note On/Off (vel 0 = off), Program Change 0-127,
  Pitch Bend (14-bit), Channel Aftertouch, CC1 Mod Wheel, CC64 Sustain.
- FX channel (default ch 2) CC 0-23: 0 Filter sw, 1 type (0-2 LPF/BPF/HPF), 2 cutoff (0-107), 3 Q (0-10),
  4 Reverb sw, 5 type (0-2 Room/Hall/Plate), 6 decay, 7 mix, 8 Delay sw, 9 decay, 10 rate, 11 mix,
  12 Dist sw, 13 gain, 14 tone, 15 level, 16 Chorus sw, 17 freq, 18 depth, 19 mix, 20 Phaser sw, 21 freq,
  22 depth, 23 mix. Switch: 0 = off, ≥1 = on; other values 0-100.
- Real-time: F8 clock (follow BPM when Sync on), FA start (from step 0), FB continue (same as start),
  FC stop.
- SysEx: `F0 43 10 pp qq vv F7` single-parameter write (param = pp*128+qq, 0-155) [MIDI]; DX7 32-voice
  bank dumps are accepted (bank select screen A/B/C/D appears).
- Sends: Note On/Off with velocity from keyboard, arp and sequencer.

### 3.12 Battery / power [M p.13-14]

2000 mAh, 3.7 V (DTP704060), about 12 h. USB-C bus power or battery. Battery icon top-right;
full-screen `LOW BATTERY` when critical. Rear slide switch for on/off.

### 3.13 Firmware history (behaviour differences) [RN]

- V09 (2026-06-24): ENV on/off switch; RATE editable while recording; SEQ rests and note replace.
- V13 (2026-07-03): ARP/SEQ BPM sync; OP page memory; voice change in the sequencer; MIDI ch (default
  ALL); PC 0-127; rates 1/1 and 1/2; global velocity 1-127.
- V15 (2026-07-30): brighter base LED level; Reset to last-imported bank A-D; Glide (GLO page 2,
  MONO only); SEQ long notes, per-pattern voice, step length, chain; Init/Rename/Copy voice;
  long-press OPx mute; FX/ENV 0-100; vel-0 note-off; FX CCs.

---

## 4. Downloaded references (`ref/`, treat as data)

| File | What |
|---|---|
| `ref/FM-1-manual.pdf` | Official detailed manual (EN p.1-16, CN p.17-30), fully read |
| `ref/manual_amazon.pdf` | Amazon-hosted printed multi-language manual (not read; covers the same product) |
| `ref/FM-1_release_notes.txt` | Firmware changelog V09/V13/V15 (GBK + EN) |
| `ref/FM-1_MIDI_EN.docx`, `ref/FM-1_MIDI_EN.txt` | Official MIDI implementation |
| `ref/products-data.js`, `ref/product.html` | Official product page data (specs, colour swatches) |
| `ref/banks/FM-1_factory_bank1-4.syx` | Community-extracted factory voices (DX7 VMEM) |
| **`ref/img/fm-1-1.png`** | **Best top-down photo (black, FX page on screen), 1000x1000** |
| `ref/img/fm-1-purple-1.png` | Top-down, purple, HOME/OSC screen (`006 STONED DX`) |
| `ref/img/fm-1-orange-1.png` | Orange colourway (top-down (?)) |
| `ref/img/fm-1-2/3/6.png` | Angled views; `fm-1-4.png` rear ports; `fm-1-5.png` underside with speaker |
| `ref/img/synthanatomy-*.jpg` | Real-unit photos 1920x1080: ENV page, FX page (`Freq: 30%`), ARP page, SEQ page (keys lit, PLAY/STOP red), algorithm diagram popup (`08`, `128 WHAT IF`), unboxing/connectivity |
| `ref/img/taoofmac-hero.jpg`, `taoofmac-ports.jpg` | Tao of Mac photos (DX7-livery unit; ports) |

Photo URLs: `http://www.cuvave.com/images/productInfo/fm-1-{1..6}.webp`,
`fm-1-purple-{1..5}.webp`, `fm-1-orange-{1..5}.webp`;
`https://synthanatomy.com/wp-content/uploads/2026/07/M-VAVE-FM-1-{review,envelope,review-effects,review-arpeggiator,review-sequencer,Review-DX-7,review-connectivity}.jpg`;
`https://taoofmac.com/space/reviews/2026/10/11/2100/{hero,ports}.jpg`.

## 5. Open questions (not answered by any source found)

- Are the keys velocity-sensitive, or is velocity the fixed GLO value?
- Exact ARP/SEQ Rate list; exact Clear gesture; whether keys play notes in EDIT/FX pages.
- Exact screen font, pixel-level layout, and true screen colours (photos disagree on saturation).
- Purpose of the tiny dots on the keybed and in the button grid; whether OCT LEDs are separate.
- Screen content for EDIT operator pages and GLO pages was not photographed (assume the same 2x2 box grid).
