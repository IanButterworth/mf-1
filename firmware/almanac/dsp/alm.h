/* Almanac's day, as the desktop app defines it in Sources/AlmanacCore: the same fields, ranges, months and
 * skies, which the parity test checks against the desktop's. The names are alm_ so the test can link both. */
#pragma once
#include <stdint.h>

#include "AlmanacCore.h"
#include "fx.h"

typedef struct {
    const char *voice_name, *month, *title, *blurb;
    uint32_t sky_top, sky_bottom, ground, far, accent;
    uint8_t season, mono, decays;
    uint8_t mod[4];      /* OP1..OP4: a modulator, which Warmth and Mood move */
    int8_t chord[5];
    uint8_t chord_size, arp_mode, arp_octaves;
    int32_t day[AL_FIELDS];
    uint8_t voice[155];  /* the month under a clear sky, VCED (OP6 first) */
} alm_month_t;

typedef struct {
    const char *name;
    uint8_t fb, wave, slow, fast, amd, quiet, loud;
    uint8_t ops[42];     /* OP6 then OP5, as VCED stores them */
} alm_sky_t;

/* How a pattern runs: the arpeggiator's mode (MF-1's numbering), octaves, rate (MF-1's rate index), gate in
 * percent and tempo in BPM. */
typedef struct {
    int on, mode, octaves, rate, gate, tempo;
} alm_pattern_t;

int alm_month_count(void);
const alm_month_t *alm_month(int index);
const alm_sky_t *alm_sky(int index);
const char *alm_place_name(int place);
const char *alm_mood_name(int mood);
void alm_default_day(int month, int32_t out[AL_FIELDS]);
void alm_range(int field, int *lo, int *hi);
void alm_clamp(int32_t day[AL_FIELDS]);
void alm_voice(const int32_t day[AL_FIELDS], uint8_t out[155]);
void alm_fx(const int32_t day[AL_FIELDS], isc_fx_params_t *out);
void alm_pattern(const int32_t day[AL_FIELDS], alm_pattern_t *out);
int alm_key_notes(const int32_t day[AL_FIELDS], int note, int left, int out[8]);
