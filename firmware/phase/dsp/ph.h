/* Phase's pieces and sounds, as the desktop app defines them in Sources/PhaseCore: the same fields, ranges, pieces
 * and sounds, which the parity test checks against the desktop's. The names are ph_ so the test can link both. The
 * processes themselves are the desktop's own process.c, copied unchanged. */
#pragma once
#include <stdint.h>

#include "PhaseCore.h"
#include "fx.h"

/* One operator as it is voiced (see the desktop's internal.h): a ratio times 100, rates and levels, and whether
 * Bright moves its level. */
typedef struct {
    int16_t ratio;
    uint8_t level, r[4], l[4], bright, detune, rate_scale, ams;
} ph_op_t;

typedef struct {
    const char *name;
    uint8_t algorithm, feedback;       /* algorithm 1 .. 32, as the DX7 numbers them */
    ph_op_t op[6];                     /* OP1 first */
    int8_t transpose;
    uint8_t lfo_speed, lfo_delay, pmd, amd, lfo_wave, pms, lfo_sync, room_type;
} ph_sound_t;

typedef struct {
    const char *voice_name, *title, *blurb;
    uint8_t cell_len;
    int8_t cell[PH_MAX_STEPS];
    int32_t s[PH_FIELDS];
} ph_piece_t;

int ph_piece_count(void);
const ph_piece_t *ph_piece(int index);
const char *ph_process_name(int process);
const char *ph_sound_name(int sound);
const char *ph_spread_name(int spread);
void ph_default_piece(int piece, int32_t out[PH_FIELDS]);
void ph_default_cell(int piece, PhaseCell *out);
void ph_range(int field, int *lo, int *hi);
void ph_clamp(int32_t s[PH_FIELDS]);
void ph_voice(const int32_t s[PH_FIELDS], uint8_t out[155]);
void ph_fx(const int32_t s[PH_FIELDS], isc_fx_params_t *out);
