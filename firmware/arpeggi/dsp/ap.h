/* Arpeggi's scenes and sounds, as the desktop app defines them in Sources/ArpeggiCore: the same fields, ranges,
 * scenes and sounds, which the parity test checks against the desktop's. The names are ap_ so the test can link
 * both. The pool and the lanes themselves are the desktop's own arp.c, copied unchanged. */
#pragma once
#include <stdint.h>

#include "ArpeggiCore.h"
#include "fx.h"

/* One operator as it is voiced (see the desktop's internal.h): a ratio times 100, rates and levels, and whether
 * Bright moves its level. */
typedef struct {
    int16_t ratio;
    uint8_t level, r[4], l[4], bright, detune, rate_scale, ams;
} ap_op_t;

typedef struct {
    const char *name;
    uint8_t algorithm, feedback;       /* algorithm 1 .. 32, as the DX7 numbers them */
    ap_op_t op[6];                     /* OP1 first */
    int8_t transpose;
    uint8_t lfo_speed, lfo_delay, pmd, amd, lfo_wave, pms, lfo_sync, room_type, chorus;
} ap_sound_t;

typedef struct {
    const char *voice_name, *title, *blurb;
    uint8_t seed_len;
    int8_t seed[AP_MAX_POOL];
    int32_t s[AP_FIELDS];
} ap_scene_t;

int ap_scene_count(void);
const ap_scene_t *ap_scene(int index);
const char *ap_shape_name(int i);
const char *ap_rate_name(int i);
const char *ap_rhythm_name(int i);
const char *ap_relation_name(int i);
const char *ap_hold_name(int i);
const char *ap_sound_name(int i);
void ap_default_scene(int scene, int32_t out[AP_FIELDS]);
int ap_default_seed(int scene, int8_t out[AP_MAX_POOL]);
void ap_range(int field, int *lo, int *hi);
void ap_clamp(int32_t s[AP_FIELDS]);
void ap_voice(const int32_t s[AP_FIELDS], uint8_t out[155]);
void ap_fx(const int32_t s[AP_FIELDS], isc_fx_params_t *out);
