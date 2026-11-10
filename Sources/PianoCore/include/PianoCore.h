// Piano: a grand piano made from measurements of a real one, in place of the DX7 voices.
//
// Every note is the measured partials of a Steinway model B (the University of Iowa's recordings, every third key at
// pp, mf and ff): their inharmonic frequencies, levels and two-stage decays with the beat between a key's strings,
// the bass's longitudinal modes, and the knock of hammer and key on the soundboard. The engine is plain C with no C
// library (piano.c), so the FM-1 firmware builds the same file and plays the same samples.
//
// The pianos are voicings of that one instrument: a harder or softer hammer, a wider unison, a shorter sustain, a
// different room. Each is a change a piano technician could make.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A piano's settings are a flat list of numbers.
enum {
  PN_F_PIANO,    // which voicing
  PN_F_TONE,     // 0 .. 100: the hammer, soft to hard; 50 as measured
  PN_F_SUSTAIN,  // 25 .. 200 percent of the measured ring
  PN_F_ROOM,     // 0 .. 100: reverb
  PN_F_SPACE,    // 0 room, 1 hall, 2 plate
  PN_F_VOLUME,   // 0 .. 100
  PN_F_UNISON,   // 0 .. 250 percent of the measured detune between a key's strings
  PN_F_KNOCK,    // 0 .. 200 percent of the measured knock
  PN_F_STRETCH,  // 0 .. 200 percent of the measured octave stretch
  PN_F_DAMPER,   // 25 .. 250 percent: how fast the dampers stop the strings
  PN_F_TOUCH,    // 1 .. 127: the velocity of the panel's keys, which sense none
  PN_FIELDS
};

int piano_count(void);
const char* piano_name(int i);
const char* piano_blurb(int i);
void piano_default(int i, int32_t out[PN_FIELDS]);
void piano_range(int field, int* lo, int* hi);
void piano_clamp(int32_t s[PN_FIELDS]);

// Puts the piano in place of the DX7 voices. Call once the engine has started.
void piano_install(void);
// Sends the settings to the piano and the effects.
void piano_apply(const int32_t s[PN_FIELDS]);
// How loud each note's strings are now, 0 silent to about 1 at ff, for the screen.
void piano_levels(float out[128]);
// Oscillators sounding, of the most the engine holds.
int piano_oscillators(void);
int piano_budget(void);

#ifdef __cplusplus
}
#endif
