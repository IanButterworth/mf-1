// Arpeggi: arpeggiators on the MF-1 engine, two at a time.
//
// The keys feed a pool of notes. Two lanes, A and B, each play the pool in a shape of their own (up, down, inward,
// shuffled, a random walk, strummed...), at a rate and in a rhythm of their own, and the relation between them
// decides how they meet: side by side, alternating note by note, answering bar by bar, in canon, mirrored, or
// trading shapes. How the keys fill the pool is the hold mode: only while held, latched as chords, or a ring of a
// fixed number of notes where each new key replaces the oldest.
//
// A scene sets all of this with a sound and its space, from dry and close to wide and echoing. Everything is
// written through MF1Core's public C API and uses only what a real FM-1 has.
//
// The patterns are plain C with integer arithmetic, no allocation and no C library, so a firmware can run them as
// they are. Every note's time is worked out from the elapsed ticks and the pool alone, never added up step by step.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AP_MAX_POOL 16
#define AP_MAX_SPAN 4
// Ticks per beat. It divides by 2, 3, 5 and 7, so every rate below is a whole number of ticks.
#define AP_PPQ 3360
// The most notes that can start at one tick: a chord on both lanes.
#define AP_MAX_OUT (2 * AP_MAX_POOL)

// How a lane walks through the pool, in the order the SHAPE knob steps through them.
enum {
  AP_UP,        // lowest to highest, through every octave of the span
  AP_DOWN,      // highest to lowest
  AP_BOUNCE,    // up and back down, not repeating the ends
  AP_ORDER,     // in the order the notes came; in a ring, slot by slot
  AP_CONVERGE,  // outside in: lowest, highest, second lowest, second highest...
  AP_THUMB,     // the lowest note between every other note, as a thumb on a pedal
  AP_SPIRAL,    // up, each pass starting one note higher
  AP_WALK,      // a random walk, a note up or down at a time
  AP_SHUFFLE,   // every note once per pass, in a new order each pass
  AP_CHORD,     // all the notes at once, an octave higher each strike through the span
  AP_STRUM,     // all the notes a moment apart, strummed down and up in turn
  AP_SHAPE_COUNT
};

// Step lengths, in the order the RATE knob steps through them.
enum {
  AP_R2, AP_R4, AP_R4D, AP_R8, AP_R8D, AP_R8T, AP_R16, AP_R16D, AP_R16T, AP_R16Q, AP_R16S, AP_R32,
  AP_RATE_COUNT
};

// Which steps strike, in the order the RHYTHM knob steps through them.
enum {
  AP_EVERY,    // every step
  AP_E7,       // 7 of every 8, spread evenly
  AP_E5,       // 5 of 8
  AP_E3,       // 3 of 8
  AP_E5_12,    // 5 of 12
  AP_RATCHET,  // every fourth step struck three times fast
  AP_GALLOP,   // one, then two in the next step: da, da-da
  AP_SKIP,     // about one step in three left out, the same ones every time round
  AP_FALL,     // a bouncing ball: strikes closer and closer together over 16 steps
  AP_RHYTHM_COUNT
};

// How the two lanes play together, in the order the RELATION knob steps through them.
enum {
  AP_TOGETHER,  // side by side, each its own shape, rate and rhythm
  AP_HOCKET,    // one line in A's time, alternate notes from A's shape and B's
  AP_ANSWER,    // A plays a bar, B answers the next
  AP_CANON,     // B follows A three steps behind, at B's pitch
  AP_MIRROR,    // B plays A's shape upside down, in B's own time
  AP_TRADE,     // side by side, swapping shapes every two bars
  AP_RELATION_COUNT
};

// How the keys fill the pool.
enum {
  AP_HOLD,   // the notes held down, and only while held
  AP_LATCH,  // a chord stays after the keys let go, until a new one starts
  AP_RING,   // a ring of Ring notes: each new key replaces the oldest, and the notes stay
  AP_HOLD_COUNT
};

// The sounds, in the order the SOUND knob steps through them.
enum { AP_PLUCK, AP_GLASS, AP_MALLET, AP_PAD, AP_CHIP, AP_BASS, AP_BELL, AP_KEYS, AP_SOUND_COUNT };

// A scene's settings are a flat list of numbers.
enum {
  AP_SCENE,       // 0 .. 7
  AP_RELATION,    // AP_TOGETHER .. AP_TRADE
  AP_DUO,         // 0: lane A alone, 1: both lanes
  AP_A_SHAPE,     // AP_UP .. AP_STRUM
  AP_A_RATE,      // AP_R2 .. AP_R32
  AP_A_RHYTHM,    // AP_EVERY .. AP_FALL
  AP_A_GATE,      // 5 .. 200: how long a note is held, in percent of its step; over 100 overlaps the next
  AP_B_SHAPE,
  AP_B_RATE,
  AP_B_RHYTHM,
  AP_B_GATE,
  AP_HOLD_MODE,   // AP_HOLD .. AP_RING
  AP_RING_SIZE,   // 1 .. 8
  AP_SPAN,        // 1 .. 4 octaves
  AP_B_PITCH,     // -24 .. 24 semitones: lane B against lane A
  AP_SOUND,       // AP_PLUCK .. AP_KEYS
  AP_BRIGHT,      // 0 .. 100, 50 as voiced
  AP_DECAY,       // 0 .. 100, 50 as voiced: how long a note rings
  AP_ROOM,        // 0 .. 100: reverb mix
  AP_TEMPO,       // 40 .. 240 BPM
  AP_SWING,       // 0 .. 100: every second step late, by up to a third of a step
  AP_ECHO,        // 0 .. 100: delay mix
  AP_ECHO_STEPS,  // 1 .. 8 sixteenths between echoes, as near as the delay can reach
  AP_FIELDS
};

typedef struct {
  char name[11];   // as stored in the voice, 10 characters
  char title[20];  // "Ring of Five"
  char blurb[48];  // a few words on what happens
} ApScene;

int arpeggi_scene_count(void);
bool arpeggi_scene(int index, ApScene* out);
const char* arpeggi_shape_name(int shape);
const char* arpeggi_rate_name(int rate);
const char* arpeggi_rhythm_name(int rhythm);
const char* arpeggi_relation_name(int relation);
const char* arpeggi_hold_name(int hold);
const char* arpeggi_sound_name(int sound);

// The settings a scene is written with, and the notes PLAY starts it with when the pool is empty.
void arpeggi_default_scene(int scene, int32_t out[AP_FIELDS]);
int arpeggi_default_seed(int scene, int8_t out[AP_MAX_POOL]);
void arpeggi_clamp(int32_t s[AP_FIELDS]);
void arpeggi_range(int field, int* lo, int* hi);

// Puts the scenes' sounds in banks A-D of the engine (the same 32 slots in each, the rest empty voices), so program
// changes, SAVE and bank export all work on them. Call after the engine started.
void arpeggi_install(void);
// Loads the scene's preset into the edit buffer, then applies its settings.
void arpeggi_load(const int32_t s[AP_FIELDS]);
// Writes the sound and the effects. Voice changes reach the next note, as on the FM-1.
void arpeggi_apply(const int32_t s[AP_FIELDS]);
// The 155 bytes of DX7 VCED the scene's sound is.
void arpeggi_voice(const int32_t s[AP_FIELDS], uint8_t out[155]);
// A DX7 32-voice bank dump (4104 bytes) of the scenes' sounds as written.
void arpeggi_export_bank(uint8_t out[4104]);

// ---------------------------------------------------------------- the pool, in plain C

// The notes the lanes play. They are kept in the order they came, or slot by slot in a ring, so Order can play
// them so. The keys held down are counted too, so Latch knows when a new chord starts.
typedef struct {
  int8_t note[AP_MAX_POOL];
  int8_t count;
  int8_t held[AP_MAX_POOL];
  int8_t held_count;
  int8_t next;  // in a full ring, the slot the next key replaces: the oldest
  int8_t hold, ring;
} ApPool;

void ap_pool_init(ApPool* p, int hold, int ring);
void ap_pool_clear(ApPool* p);
// Changes how the keys fill the pool. A smaller ring keeps its newest notes; leaving Hold keeps what is held.
void ap_pool_mode(ApPool* p, int hold, int ring);
// A key goes down or up. True when the pool was empty and now has notes, so the clock should start.
bool ap_pool_key_down(ApPool* p, int note);
void ap_pool_key_up(ApPool* p, int note);
// Puts these notes in the pool as if latched, whatever the hold mode.
void ap_pool_seed(ApPool* p, const int8_t* notes, int n);
// The pool's notes, sorted low to high. Returns how many.
int ap_pool_sorted(const ApPool* p, int8_t out[AP_MAX_POOL]);

// ---------------------------------------------------------------- the lanes, in plain C

typedef struct {
  int relation, duo;
  int shape[2], rate[2], rhythm[2], gate[2];
  int span, pitch, swing, tempo;
  int transpose;  // added to every note: the octave buttons
} ApProc;

void ap_proc_make(const int32_t s[AP_FIELDS], ApProc* out);
// A rate's step in ticks.
int ap_rate_ticks(int rate);
// The lane's shape at a tick: Trade swaps them every two bars.
int ap_shape_at(const ApProc* p, int lane, int64_t tick);

// One note a lane starts: when (in ticks from the clock's start), which lane, its strike count on that lane, the
// MIDI note and how long it is held (in ticks).
typedef struct {
  int64_t tick;
  int lane, strike, note;
  int32_t length;
} ApNote;

// The notes the lanes start at the first tick at or after `from` and before `to` that has any, lane A first.
// Returns how many, 0 if none.
int ap_next_notes(const ApProc* p, const ApPool* pool, int64_t from, int64_t to, ApNote out[AP_MAX_OUT]);

// ---------------------------------------------------------------- the player

// The player runs the lanes on its own high-priority thread and presses the engine's keys when each note falls due.
// Notes that two lanes (or a lane and a key played along) share are counted, so one letting go never cuts another.
// Every call is safe from the main thread.

// New settings. A tempo change keeps the time; anything else takes effect from the next note.
void arpeggi_set(const ApProc* p);
// How the keys fill the pool.
void arpeggi_pool_mode(int hold, int ring);
// The keys, feeding the pool. A key that finds the clock stopped starts it at once, its first note with it.
void arpeggi_key_down(int note);
void arpeggi_key_up(int note);
// Keys that play along over the lanes, sounding at once and leaving the pool alone.
void arpeggi_play_down(int note);
void arpeggi_play_up(int note);
// Fills the pool as if latched and starts the clock.
void arpeggi_seed(const int8_t* notes, int n);
// Starts the clock from its beginning, if the pool has notes; false if it is empty.
bool arpeggi_start(void);
// Stops the clock and lets go of every note the lanes hold. The pool stays.
void arpeggi_stop(void);
// Empties the pool and stops.
void arpeggi_clear(void);
bool arpeggi_playing(void);
void arpeggi_pool(ApPool* out);
// The clock's tick now; false when it is stopped.
bool arpeggi_now(int64_t* tick);

// The notes the lanes started lately, newest first, with how long ago in milliseconds.
typedef struct {
  int lane, note;
  int32_t ago_ms, length_ms;
} ApHit;
int arpeggi_recent(ApHit* out, int max);

// Stops, lets go of every note and stops the thread. Call before the engine stops.
void arpeggi_shutdown(void);
// How many holders a note has, lanes and keys together.
int arpeggi_refs(int note);
// The thread's timing: notes and offs fired, and their mean and worst lateness in microseconds.
void arpeggi_timing(double out[3]);

// Offline, for tests: there is no thread, and time only moves when a test seeks, firing what falls due.
void arpeggi_offline(bool on);
void arpeggi_offline_seek(double seconds);

#ifdef __cplusplus
}
#endif
