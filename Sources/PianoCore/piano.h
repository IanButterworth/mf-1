// A modal piano: every note is a set of measured inharmonic partials, each a pair of decaying oscillators.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PN_BLOCK 64
#ifndef PN_MODES
#define PN_MODES 384  // the most oscillators a piano can hold: what the FM-1's CPU can carry (estimated)
#endif
#define PN_NOTES 128

typedef struct {
  uint16_t first, count, xfirst, xcount;  // its partials, and its peaks off the harmonic series
  float B, cents;
  float mf_c0, mf_c1, pp_c0, pp_c1;  // dB below ff, and dB per doubling of the partial number
} pn_note_t;

typedef struct {
  float lvl, d2, k1, k2, beat;
} pn_partial_t;

typedef struct {
  float f, lvl, d2, k1, k2;
} pn_extra_t;

#define PN_BOARD 32
#define PN_STRIKES 8

// A partial a struck note would like to sound, before the pool decides which it gets.
typedef struct {
  float a, f, k, krel;
} pn_cand_t;
#define PN_CANDS (2 * 128 + 2 * 16)

typedef struct {
  float sr;
  int budget;                // oscillators in use at most (the CPU's limit)
  int count;                 // oscillators sounding, packed at the front
  float zr[PN_MODES], zi[PN_MODES], wr[PN_MODES], wi[PN_MODES], mag2[PN_MODES];
  float r0[PN_MODES], rd[PN_MODES];  // each sample's shrink, undamped and damped
  uint8_t note[PN_MODES];
  uint8_t held[PN_NOTES];    // keys down
  uint8_t damped[PN_NOTES];  // the note's damper is on its strings
  uint8_t pedal;
  float gain;
  float cull2;               // an oscillator below this squared amplitude stops
  // The knock: the hammer's blow, through the key and bridge, rings the soundboard's own modes. One board serves every
  // note, so it costs the same however many notes sound. Each strike is a short burst of noise, as long as the
  // hammer touches the string, that each key feeds to the modes in its own proportions.
  uint32_t noise;
  float board_f[PN_BOARD], board_c1[PN_BOARD], board_c2[PN_BOARD], board_b[PN_BOARD];
  float board_y1[PN_BOARD], board_y2[PN_BOARD];
  float board_db[PN_BOARD];  // each mode's level at ff, fitted to the recordings
  struct {
    float g[PN_BOARD];
    int t, len;
  } strike[PN_STRIKES];
  int board_live;            // samples until the board has fallen silent
  float knock_db, knock_slope, knock_vel, knock_tilt;  // level at ff and middle C, dB per key, per dB of the strings
  int strings_off;                                       // for measuring the knock alone
  // The player's shaping, applied as each note is struck. 0 or 1 leaves the measured piano as it is.
  float tilt;        // dB per doubling of the partial number: a harder or softer hammer
  float decay;       // the strings' decay rates are multiplied by this
  float beat;        // how far apart each key's strings are tuned, times the measured
  float knock;       // the knock's level, times the measured
  float stretch;     // the octaves' stretch, times the measured
  float release;     // the dampers' rates, times the inferred
  float soft_db;     // the top's extra fall per octave at mf
  float level[PN_NOTES];  // each note's sound, squared amplitude summed, for the screen
  pn_cand_t cand[PN_CANDS];  // a note-on's scratch, here rather than on the audio interrupt's small stack
} pn_piano_t;

void pn_init(pn_piano_t* p, float sr);
void pn_note_on(pn_piano_t* p, int note, int vel);
void pn_note_off(pn_piano_t* p, int note);
void pn_pedal(pn_piano_t* p, int down);
void pn_all_off(pn_piano_t* p);
// One block of PN_BLOCK samples, mono, written to out.
void pn_render(pn_piano_t* p, float* out);

// A piano's setting (PianoCore.h's PN_F_ fields) into the engine, and its effects as the FM-1's 24 effect values.
void piano_setting(pn_piano_t* p, int id, int v);
void piano_fx(const int32_t* settings, int cc[24]);

#ifdef __cplusplus
}
#endif
