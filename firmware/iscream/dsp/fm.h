/*
 * Copyright 2012 Google Inc.
 * Modifications copyright 2026 the MF-1 authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* A C port of MF-1's msfa engine for the FM-1 firmware, fixed at 44.1 kHz with the
 * FM-1 V15 behaviour always on. With the same patch and notes it gives the same samples as MF-1. */
#pragma once
#include <stdint.h>

#define FM_LG_N 6
#define FM_N (1 << FM_LG_N)
#define FM_VOICE_SIZE 156
#define FM_OP_ENABLE 155

typedef struct {
    int rates[4], levels[4];
    int outlevel, rate_scaling;
    int32_t level, targetlevel;
    int rising, ix, down;
    int32_t inc, staticcount;
} fm_env_t;

typedef struct {
    int rates[4], levels[4];
    int32_t level, targetlevel;
    int rising, ix, down;
    int32_t inc;
} fm_pitchenv_t;

typedef struct {
    uint32_t phase, delta;
    uint8_t waveform, randstate, sync;
    uint32_t delaystate, delayinc, delayinc2;
} fm_lfo_t;

typedef struct {
    int32_t gain[2];
    int32_t freq;
    uint32_t phase;
} fm_op_t;

typedef struct {
    int32_t pitch_bend;          /* 14 bit, 0x2000 is centre */
    int bend_up, bend_down;      /* semitones */
    int mod_wheel;               /* 0..127, to pitch at full range, as the FM-1 */
    int aftertouch;              /* 0..127: stock V15 turns channel pressure into vibrato too */
} fm_ctrl_t;

typedef struct {
    int32_t buf[2][FM_N];
    fm_env_t env[6];
    fm_op_t op[6];
    fm_pitchenv_t pitchenv;
    int32_t basepitch[6];
    int32_t fb_buf[2];
    int fb_shift, algorithm;
    int pitchmoddepth, pitchmodsens, ampmoddepth;
    int32_t ampmodsens[6];
    int op_enable;
    int midinote, velocity;
    int32_t last_level[6];
} fm_voice_t;

void fm_ctrl_init(fm_ctrl_t *c);
void fm_lfo_reset(fm_lfo_t *l, const uint8_t params[6]);
int32_t fm_lfo_sample(fm_lfo_t *l);
int32_t fm_lfo_delay(fm_lfo_t *l);
void fm_lfo_keydown(fm_lfo_t *l);

void fm_voice_init(fm_voice_t *v, const uint8_t patch[FM_VOICE_SIZE], int midinote, int velocity, int keep_phase);
void fm_voice_update(fm_voice_t *v, const uint8_t patch[FM_VOICE_SIZE]);
/* Writes one 64-sample block, Q24, before the mix scales each voice down by 16. */
void fm_voice_compute(fm_voice_t *v, int32_t *out, int32_t lfo_val, int32_t lfo_delay, const fm_ctrl_t *c);
void fm_voice_keyup(fm_voice_t *v);
void fm_voice_transfer(fm_voice_t *v, int midinote);
int fm_voice_finished(const fm_voice_t *v);
