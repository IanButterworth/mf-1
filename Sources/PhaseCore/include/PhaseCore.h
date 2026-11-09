// Phase: process music on the MF-1 engine, as in the minimalists' tape and phase pieces of the 1960s and 70s.
//
// A piece is a short cell of notes and rests, a sound and a process. The process turns the cell into music: two or
// more parts play it and drift apart, jump ahead, fill in, follow in canon, pulse, stretch or loop at slightly
// different speeds. The keys write a new cell. Everything is written through MF1Core's public C API and uses only
// what a real FM-1 has, so a piece sounds the same on the hardware.
//
// The processes themselves are plain C with integer arithmetic and no allocation, so a firmware can run them too.
// Every part's position is computed from the elapsed time, never added up step by step, so a drift is exact however
// long the piece runs.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PH_MAX_STEPS 16
#define PH_MAX_PARTS 4
// Ticks per step. A step is a sixteenth note; at 120 BPM a tick is about 0.13 ms.
#define PH_TPS 960

// The processes, in the order the PROCESS knob steps through them.
enum {
  PH_PHASE,    // part 2 runs a little faster until it is a step ahead, holds there, then drifts again
  PH_SHIFT,    // part 2 jumps a step ahead every few repeats, with no drift between
  PH_BUILD,    // notes fill the cell's rests one at a time, then empty out again
  PH_CANON,    // parts enter one after another, each a few steps behind, then leave
  PH_PULSE,    // the cell's notes become a chord, pulsed on every step, swelling in waves
  PH_STRETCH,  // notes get longer each stage until they overlap into a held chord; later parts play slower
  PH_RESULT,   // two parts phase, and a third picks a line out of what they play together
  PH_TAPE,     // loops of slightly different lengths drift apart and never lock
  PH_PROCESS_COUNT
};

// The sounds, in the order the SOUND knob steps through them.
enum { PH_MARIMBA, PH_VIBES, PH_GLOCK, PH_PIANO, PH_ORGAN, PH_WOOD, PH_VOICE, PH_BELL, PH_SOUND_COUNT };

// How far apart the parts sit in pitch: all in unison, the upper parts an octave up, or spread wider.
enum { PH_UNISON, PH_CLOSE, PH_OPEN, PH_WIDE, PH_SPREAD_COUNT };

// A piece's settings are a flat list of numbers.
enum {
  PH_PIECE,       // 0 .. 7
  PH_PROCESS,     // PH_PHASE .. PH_TAPE
  PH_MOTION,      // 0 .. 100: the process's own amount (drift speed, jump, offset, gap, swell, growth)
  PH_PARTS,       // 1 .. 4
  PH_REPEATS,     // 1 .. 16: repeats of the cell in each stage of the process
  PH_LENGTH,      // 2 .. 16 steps
  PH_DENSITY,     // 0 .. 100: how many of the cell's notes play, 100 is all of them
  PH_TRANSPOSE,   // -12 .. 12 semitones
  PH_GATE,        // 5 .. 100: how long a note is held, in percent of a step
  PH_SOUND,       // PH_MARIMBA .. PH_BELL
  PH_BRIGHT,      // 0 .. 100, 50 as voiced
  PH_DECAY,       // 0 .. 100, 50 as voiced: how long a note rings
  PH_ROOM,        // 0 .. 100: reverb mix
  PH_TEMPO,       // 40 .. 200 BPM, four steps to a beat
  PH_ECHO,        // 0 .. 100: delay mix
  PH_ECHO_STEPS,  // 1 .. 8: steps between echoes, as near as the delay can reach
  PH_SPREAD,      // PH_UNISON .. PH_WIDE
  PH_FIELDS
};

// A cell: up to 16 steps, each a MIDI note or -1 for a rest.
typedef struct {
  int8_t note[PH_MAX_STEPS];
  int8_t length;
} PhaseCell;

typedef struct {
  char name[11];   // as stored in the voice, 10 characters
  char title[20];  // "Two Mallets"
  char blurb[40];  // a few words on what happens
} PhasePiece;

int phase_piece_count(void);
bool phase_piece(int index, PhasePiece* out);
const char* phase_process_name(int process);
const char* phase_sound_name(int sound);
const char* phase_spread_name(int spread);

// The settings and the cell a piece is written with.
void phase_default_piece(int piece, int32_t out[PH_FIELDS]);
void phase_default_cell(int piece, PhaseCell* out);
void phase_clamp(int32_t s[PH_FIELDS]);
void phase_range(int field, int* lo, int* hi);

// Puts the pieces' sounds in banks A-D of the engine (the same 32 slots in each, the rest empty voices), so program
// changes, SAVE and bank export all work on them. Call after the engine started.
void phase_install(void);
// Loads the piece's preset into the edit buffer, then applies its settings.
void phase_load(const int32_t s[PH_FIELDS]);
// Writes the sound and the effects. Voice changes reach the next note, as on the FM-1.
void phase_apply(const int32_t s[PH_FIELDS]);
// The 155 bytes of DX7 VCED the piece's sound is.
void phase_voice(const int32_t s[PH_FIELDS], uint8_t out[155]);
// A DX7 32-voice bank dump (4104 bytes) of the pieces' sounds as written.
void phase_export_bank(uint8_t out[4104]);

// ---------------------------------------------------------------- the process, in plain C

// Everything the process needs, worked out once from the settings and the cell.
typedef struct {
  int process, parts, length, repeats, motion, gate, transpose, spread, tempo;
  int8_t note[PH_MAX_STEPS];   // the cell after Length and Density; -1 is a rest
  int8_t fill[PH_MAX_STEPS];   // for Build: the note that fills each rest
  int8_t order[PH_MAX_STEPS];  // for Build: the order the rests fill in
  int rests;
} PhaseProc;

void phase_proc_make(const int32_t s[PH_FIELDS], const PhaseCell* cell, PhaseProc* out);
// What Motion means for a process, as a number: repeats to drift a step (Phase, Result, Tape), steps a jump
// (Shift), steps behind (Build, Canon), or Motion itself (Pulse's swell, Stretch's growth).
int phase_motion_steps(int process, int motion, int length);
// True when two processes give every part the same positions, so a change between them need not restart.
bool phase_proc_same_motion(const PhaseProc* a, const PhaseProc* b);

// One note a part starts: when (in ticks from the start), which part, which step of the part's own count, the MIDI
// note, and how long it is held (in ticks).
typedef struct {
  int64_t tick;
  int part, step, note;
  int32_t length;
} PhaseNote;

// The notes the parts start at the first tick at or after `from` and before `to` that has any, at most one per
// part, in part order. Returns how many, 0 if there are none.
int phase_next_notes(const PhaseProc* p, int64_t from, int64_t to, PhaseNote out[PH_MAX_PARTS]);

// Where one part is at a tick, for the screen.
typedef struct {
  bool active;
  int32_t pos_milli;                // the play position in the cell, 0 .. length*1000
  int8_t note[PH_MAX_STEPS];        // what each step plays now, -1 for nothing
  int32_t len_milli[PH_MAX_STEPS];  // how long each note is, in thousandths of the part's own steps
} PhasePartView;

// Where the process is at a tick.
typedef struct {
  int parts, length;
  // Part 2's lead over part 1 in thousandths of a step: signed, and wrapped round the cell (0 .. length*1000).
  int32_t lead_milli, offset_milli;
  bool drifting;
  int stage, stages;  // where the process is in its cycle of stages
  PhasePartView part[PH_MAX_PARTS];
} PhaseView;

void phase_view(const PhaseProc* p, int64_t tick, PhaseView* out);

// ---------------------------------------------------------------- the player

// The player runs the process on its own high-priority thread and presses the engine's keys at the right times.
// Notes that two parts (or a part and a held key) share are counted, so one part letting go never cuts another.
// Every call is safe from the main thread.

// Starts the process from its beginning, `lead` seconds from now.
void phase_start(const PhaseProc* p, double lead);
// A change while playing. Tempo changes keep the time; a change to how the parts move restarts the process on the
// next step; anything else (notes, gate, transpose) takes effect from the next note.
void phase_change(const PhaseProc* p);
// Stops and lets go of every note the process holds.
void phase_stop(void);
bool phase_playing(void);
// Where the process is now. False when it is not playing.
bool phase_now(PhaseView* out);
// The keys: they sound at once and share the note counts with the process.
void phase_key_down(int note);
void phase_key_up(int note);
// Stops, lets go of every note and stops the thread. Call before the engine stops.
void phase_shutdown(void);
// How many holders a note has, process and keys together.
int phase_refs(int note);
// The thread's timing: notes and offs fired, and their mean and worst lateness in microseconds.
void phase_timing(double out[3]);

// Offline, for tests: there is no thread, and time only moves when a test seeks, firing what falls due.
void phase_offline(bool on);
void phase_offline_seek(double seconds);

#ifdef __cplusplus
}
#endif
