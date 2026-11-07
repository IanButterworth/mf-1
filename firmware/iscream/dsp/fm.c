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

#include "fm.h"

#include "isc_libc.h"

#include "fm_tables.inc"

#define SIN_SHIFT (24 - 10)
#define FREQ_SHIFT (24 - 10)
#define MAX_LOGFREQ_INT 20

/* The FM-1's CPU can corrupt a register when a 64-bit multiply sits beside a load, so no 64-bit multiply is
 * inlined here: the once-a-block ones go through this function, and the per-sample ones are done in 32 bits. */
__attribute__((noinline)) static int64_t mul64(int64_t a, int64_t b) { return a * b; }

/* (a * b) >> 24 for |a|, |b| < 2^31, exactly as the 64-bit product would give, from four 32-bit products. */
static inline int32_t mul_q24(int32_t a, int32_t b)
{
    int32_t ah = a >> 12, al = a & 0xFFF, bh = b >> 12, bl = b & 0xFFF;
    int32_t mid = ah * bl + al * bh;
    return ah * bh + ((mid + ((al * bl) >> 12)) >> 12);
}

/* The sine table's steps are under 2^17 and the fraction under 2^14, so the product fits in 32 bits. */
static inline int32_t sin_lookup(int32_t phase)
{
    int lowbits = phase & ((1 << SIN_SHIFT) - 1);
    int ix = (phase >> (SIN_SHIFT - 1)) & ((1024 - 1) << 1);
    int dy = kSinTab[ix];
    int y0 = kSinTab[ix + 1];
    return y0 + ((dy * lowbits) >> SIN_SHIFT);
}

static inline int32_t exp2_lookup(int32_t x)
{
    int lowbits = x & ((1 << SIN_SHIFT) - 1);
    int ix = (x >> (SIN_SHIFT - 1)) & ((1024 - 1) << 1);
    int dy = kExp2Tab[ix];
    int y0 = kExp2Tab[ix + 1];
    int y = y0 + (int)(mul64(dy, lowbits) >> SIN_SHIFT);
    int sh = 6 - (x >> 24);
    if (sh >= 31)
        return 0;
    return y >> sh;
}

static int32_t freq_lookup(int32_t logfreq)
{
    int ix, lowbits, hibits;
    int32_t y0, y1, y;
    if (logfreq < 0)
        return 0;
    ix = (logfreq & 0xffffff) >> FREQ_SHIFT;
    y0 = kFreqTab[ix];
    y1 = kFreqTab[ix + 1];
    lowbits = logfreq & ((1 << FREQ_SHIFT) - 1);
    y = y0 + (int32_t)(mul64(y1 - y0, lowbits) >> FREQ_SHIFT);
    hibits = logfreq >> 24;
    if (hibits > MAX_LOGFREQ_INT)
        return y;
    return y >> (MAX_LOGFREQ_INT - hibits);
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ envelope */

static const int levellut[] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};

static int scaleoutlevel(int outlevel)
{
    return outlevel >= 20 ? 28 + outlevel : levellut[imax(0, outlevel)];
}

/* Hold times, in 44.1 kHz samples, for a stage whose target equals the current level. */
static const int statics[77] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600,
    584325,  507150,  502740,  441000,  418950,  352800,  308700, 286650, 253575, 220500,
    220500,  176400,  145530,  145530,  125685,  110250,  110250, 88200,  88200,  74970,
    61740,   61740,   55125,   48510,   44100,   37485,   31311,  30870,  27562,  27562,
    22050,   18522,   17640,   15435,   14112,   13230,   11025,  9261,   9261,   7717,
    6615,    6615,    5512,    5512,    4410,    3969,    3969,   3439,   2866,   2690,
    2249,    1984,    1896,    1808,    1411,    1367,    1234,   1146,   926,    837,
    837,     705,     573,     573,     529,     441,     441};

static void env_advance(fm_env_t *e, int newix)
{
    int newlevel, actuallevel, qrate;
    e->ix = newix;
    if (e->ix >= 4)
        return;
    newlevel = e->levels[e->ix];
    actuallevel = scaleoutlevel(newlevel) >> 1;
    actuallevel = (actuallevel << 6) + e->outlevel - 4256;
    actuallevel = actuallevel < 16 ? 16 : actuallevel;
    e->targetlevel = actuallevel << 16;
    e->rising = e->targetlevel > e->level;
    qrate = (e->rates[e->ix] * 41) >> 6;
    qrate += e->rate_scaling;
    qrate = imin(qrate, 63);
    if (e->targetlevel == e->level || (e->ix == 0 && newlevel == 0)) {
        int staticrate = imin(e->rates[e->ix] + e->rate_scaling, 99);
        e->staticcount = staticrate < 77 ? statics[staticrate] : 20 * (99 - staticrate);
        if (staticrate < 77 && e->ix == 0 && newlevel == 0)
            e->staticcount /= 20;
    } else {
        e->staticcount = 0;
    }
    e->inc = (4 + (qrate & 3)) << (2 + FM_LG_N + (qrate >> 2));
}

static void env_set(fm_env_t *e, const int r[4], const int l[4], int ol, int rate_scaling)
{
    isc_memcpy(e->rates, r, sizeof e->rates);
    isc_memcpy(e->levels, l, sizeof e->levels);
    e->outlevel = ol;
    e->rate_scaling = rate_scaling;
}

static void env_init(fm_env_t *e, const int r[4], const int l[4], int ol, int rate_scaling)
{
    env_set(e, r, l, ol, rate_scaling);
    e->level = 0;
    e->down = 1;
    env_advance(e, 0);
}

static void env_update(fm_env_t *e, const int r[4], const int l[4], int ol, int rate_scaling)
{
    env_set(e, r, l, ol, rate_scaling);
    /* Only the sustain stage is re-aimed, so an edit while a key is held does not click. */
    if (e->down && e->ix == 3)
        env_advance(e, 3);
}

static int32_t env_sample(fm_env_t *e)
{
    if (e->staticcount) {
        e->staticcount -= FM_N;
        if (e->staticcount <= 0) {
            e->staticcount = 0;
            env_advance(e, e->ix + 1);
        }
    }
    if (e->ix < 3 || (e->ix < 4 && !e->down)) {
        if (e->staticcount) {
        } else if (e->rising) {
            const int jumptarget = 1716;
            if (e->level < (jumptarget << 16))
                e->level = jumptarget << 16;
            e->level += (((17 << 24) - e->level) >> 24) * e->inc;
            if (e->level >= e->targetlevel) {
                e->level = e->targetlevel;
                env_advance(e, e->ix + 1);
            }
        } else {
            e->level -= e->inc;
            if (e->level <= e->targetlevel) {
                e->level = e->targetlevel;
                env_advance(e, e->ix + 1);
            }
        }
    }
    return e->level;
}

static void env_keydown(fm_env_t *e, int d)
{
    if (e->down != d) {
        e->down = d;
        env_advance(e, d ? 0 : 3);
    }
}

/* ------------------------------------------------------------------ pitch envelope */

static const uint8_t pitchratetab[] = {
    1,   2,   3,   3,   4,   4,   5,   5,   6,   6,   7,   7,   8,   8,   9,   9,   10,
    10,  11,  11,  12,  12,  13,  13,  14,  14,  15,  16,  16,  17,  18,  18,  19,  20,
    21,  22,  23,  24,  25,  26,  27,  28,  30,  31,  33,  34,  36,  37,  38,  39,  41,
    42,  44,  46,  47,  49,  51,  53,  54,  56,  58,  60,  62,  64,  66,  68,  70,  72,
    74,  76,  79,  82,  85,  88,  91,  94,  98,  102, 106, 110, 115, 120, 125, 130, 135,
    141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};

static const int8_t pitchtab[] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35,
    -33,  -32,  -31,  -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17,
    -16,  -15,  -14,  -13, -12, -11, -10, -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,  0,
    1,    2,    3,    4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,
    18,   19,   20,   21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,
    35,   38,   40,   43,  46,  49,  53,  58,  65,  73,  82,  92,  103, 115, 127};

static void penv_advance(fm_pitchenv_t *p, int newix)
{
    p->ix = newix;
    if (p->ix < 4) {
        p->targetlevel = pitchtab[p->levels[p->ix]] << 19;
        p->rising = p->targetlevel > p->level;
        p->inc = pitchratetab[p->rates[p->ix]] * FM_PITCHENV_UNIT;
    }
}

static void penv_set(fm_pitchenv_t *p, const int r[4], const int l[4])
{
    isc_memcpy(p->rates, r, sizeof p->rates);
    isc_memcpy(p->levels, l, sizeof p->levels);
    p->level = pitchtab[l[3]] << 19;
    p->down = 1;
    penv_advance(p, 0);
}

static void penv_update(fm_pitchenv_t *p, const int r[4], const int l[4])
{
    isc_memcpy(p->rates, r, sizeof p->rates);
    isc_memcpy(p->levels, l, sizeof p->levels);
    if (p->ix < 4)
        penv_advance(p, p->ix);
}

static int32_t penv_sample(fm_pitchenv_t *p)
{
    if (p->ix < 3 || (p->ix < 4 && !p->down)) {
        if (p->rising) {
            p->level += p->inc;
            if (p->level >= p->targetlevel) {
                p->level = p->targetlevel;
                penv_advance(p, p->ix + 1);
            }
        } else {
            p->level -= p->inc;
            if (p->level <= p->targetlevel) {
                p->level = p->targetlevel;
                penv_advance(p, p->ix + 1);
            }
        }
    }
    return p->level;
}

static void penv_keydown(fm_pitchenv_t *p, int d)
{
    if (p->down != d) {
        p->down = d;
        penv_advance(p, d ? 0 : 3);
    }
}

/* ------------------------------------------------------------------ LFO */

void fm_lfo_reset(fm_lfo_t *l, const uint8_t params[6])
{
    int rate = params[0];
    int sr = rate == 0 ? 1 : (165 * rate) >> 6;
    int a = 99 - params[1];
    sr *= sr < 160 ? 11 : (11 + ((sr - 160) >> 4));
    l->delta = FM_LFO_UNIT * (uint32_t)sr;
    if (a == 99) {
        l->delayinc = ~0u;
        l->delayinc2 = ~0u;
    } else {
        a = (16 + (a & 15)) << (1 + (a >> 4));
        l->delayinc = FM_LFO_UNIT * (uint32_t)a;
        a &= 0xff80;
        a = imax(0x80, a);
        l->delayinc2 = FM_LFO_UNIT * (uint32_t)a;
    }
    l->waveform = params[5];
    l->sync = params[4] != 0;
}

int32_t fm_lfo_sample(fm_lfo_t *l)
{
    int32_t x;
    l->phase += l->delta;
    switch (l->waveform) {
    case 0: /* triangle */
        x = (int32_t)(l->phase >> 7);
        x ^= -(int32_t)(l->phase >> 31);
        x &= (1 << 24) - 1;
        return x;
    case 1: /* saw down */
        return (int32_t)((~l->phase ^ (1u << 31)) >> 8);
    case 2: /* saw up */
        return (int32_t)((l->phase ^ (1u << 31)) >> 8);
    case 3: /* square */
        return (int32_t)(((~l->phase) >> 7) & (1 << 24));
    case 4: /* sine */
        return (1 << 23) + (sin_lookup((int32_t)(l->phase >> 8)) >> 1);
    case 5: /* sample and hold */
        if (l->phase < l->delta)
            l->randstate = (uint8_t)((l->randstate * 179 + 17) & 0xff);
        x = l->randstate ^ 0x80;
        return (x + 1) << 16;
    }
    return 1 << 23;
}

int32_t fm_lfo_delay(fm_lfo_t *l)
{
    uint32_t delta = l->delaystate < (1u << 31) ? l->delayinc : l->delayinc2;
    uint64_t d = (uint64_t)l->delaystate + delta;
    if (d > ~0u)
        return 1 << 24;
    l->delaystate = (uint32_t)d;
    if (d < (1u << 31))
        return 0;
    return (int32_t)((d >> 7) & ((1 << 24) - 1));
}

void fm_lfo_keydown(fm_lfo_t *l)
{
    if (l->sync)
        l->phase = (1u << 31) - 1;
    l->delaystate = 0;
}

/* ------------------------------------------------------------------ algorithms */

enum {
    OUT_BUS_ONE = 1 << 0,
    OUT_BUS_TWO = 1 << 1,
    OUT_BUS_ADD = 1 << 2,
    IN_BUS_ONE = 1 << 4,
    IN_BUS_TWO = 1 << 5,
    FB_IN = 1 << 6,
    FB_OUT = 1 << 7
};

/* Routing per algorithm, operators in VCED order (OP6 first). */
static const uint8_t algorithms[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14}, {0x41, 0x11, 0x94, 0x01, 0x11, 0x14},
    {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0x41, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14},
    {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14},
    {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14},
    {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14},
    {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14},
    {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14}, {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04},
    {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14},
    {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04},
};

static int carrier_mask(int algorithm)
{
    int mask = 0, i;
    for (i = 0; i < 6; i++)
        if ((algorithms[algorithm & 31][i] & 3) == 0)
            mask |= 1 << (5 - i);
    return mask;
}

static void op_compute(int32_t *out, const int32_t *in, uint32_t phase, uint32_t freq, int32_t gain1,
                       int32_t gain2, int add)
{
    int32_t dgain = (gain2 - gain1 + (FM_N >> 1)) >> FM_LG_N;
    int32_t gain = gain1;
    int i;
    for (i = 0; i < FM_N; i++) {
        int32_t y, v;
        gain += dgain;
        y = sin_lookup((int32_t)(phase + (uint32_t)in[i]));
        v = mul_q24(y, gain);
        out[i] = add ? out[i] + v : v;
        phase += freq;
    }
}

static void op_compute_pure(int32_t *out, uint32_t phase, uint32_t freq, int32_t gain1, int32_t gain2, int add)
{
    int32_t dgain = (gain2 - gain1 + (FM_N >> 1)) >> FM_LG_N;
    int32_t gain = gain1;
    int i;
    for (i = 0; i < FM_N; i++) {
        int32_t y, v;
        gain += dgain;
        y = sin_lookup((int32_t)phase);
        v = mul_q24(y, gain);
        out[i] = add ? out[i] + v : v;
        phase += freq;
    }
}

static void op_compute_fb(int32_t *out, uint32_t phase, uint32_t freq, int32_t gain1, int32_t gain2,
                          int32_t *fb_buf, int fb_shift, int add)
{
    int32_t dgain = (gain2 - gain1 + (FM_N >> 1)) >> FM_LG_N;
    int32_t gain = gain1;
    int32_t y0 = fb_buf[0];
    int32_t y = fb_buf[1];
    int i;
    for (i = 0; i < FM_N; i++) {
        int32_t scaled_fb;
        gain += dgain;
        scaled_fb = (y0 + y) >> (fb_shift + 1);
        y0 = y;
        y = sin_lookup((int32_t)(phase + (uint32_t)scaled_fb));
        y = mul_q24(y, gain);
        out[i] = add ? out[i] + y : y;
        phase += freq;
    }
    fb_buf[0] = y0;
    fb_buf[1] = y;
}

static void core_compute(fm_voice_t *v, int32_t *output)
{
    const int kLevelThresh = 1120;
    const uint8_t *alg = algorithms[v->algorithm & 31];
    int has_contents[3] = {1, 0, 0};
    int op;
    for (op = 0; op < 6; op++) {
        int flags = alg[op];
        int add = (flags & OUT_BUS_ADD) != 0;
        fm_op_t *p = &v->op[op];
        int inbus = (flags >> 4) & 3;
        int outbus = flags & 3;
        int32_t *outptr = outbus == 0 ? output : v->buf[outbus - 1];
        int32_t gain1 = p->gain[0];
        int32_t gain2 = p->gain[1];
        if (gain1 >= kLevelThresh || gain2 >= kLevelThresh) {
            if (!has_contents[outbus])
                add = 0;
            if (inbus == 0 || !has_contents[inbus]) {
                if ((flags & 0xc0) == 0xc0 && v->fb_shift < 16)
                    op_compute_fb(outptr, p->phase, (uint32_t)p->freq, gain1, gain2, v->fb_buf, v->fb_shift, add);
                else
                    op_compute_pure(outptr, p->phase, (uint32_t)p->freq, gain1, gain2, add);
            } else {
                op_compute(outptr, v->buf[inbus - 1], p->phase, (uint32_t)p->freq, gain1, gain2, add);
            }
            has_contents[outbus] = 1;
        } else if (!add) {
            has_contents[outbus] = 0;
        }
        p->phase += (uint32_t)p->freq << FM_LG_N;
    }
}

/* ------------------------------------------------------------------ voice */

static int32_t midinote_to_logfreq(int midinote)
{
    const int base = 50857777; /* (1 << 24) * (log2(440) - 69/12) */
    const int step = (1 << 24) / 12;
    return base + step * midinote;
}

static const int32_t coarsemul[] = {
    -16777216, 0,        16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
    50331648,  53182516, 55732705, 58039632, 60145690, 62083076, 63876816, 65546747,
    67108864,  68576247, 69959732, 71268397, 72509921, 73690858, 74816848, 75892776,
    76922906,  77910978, 78860292, 79773775, 80654032, 81503396, 82323963, 83117622};

static int32_t osc_freq(int midinote, int mode, int coarse, int fine, int detune)
{
    int32_t logfreq;
    if (mode == 0) {
        logfreq = midinote_to_logfreq(midinote);
        logfreq += coarsemul[coarse & 31];
        logfreq += kFineTab[fine];
        /* FM-1 V15 detunes about 2.8 cents per step instead of 0.96, a little wider going up. */
        logfreq += (detune > 7 ? 40800 : 37600) * (detune - 7);
    } else {
        logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
        logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
    }
    return logfreq;
}

static const uint8_t velocity_data[64] = {
    0,   70,  86,  97,  106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
    166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202, 205,
    206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232, 233, 235,
    237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};

static int scale_velocity(int velocity, int sensitivity)
{
    int clamped = imax(0, imin(127, velocity));
    int vel_value = velocity_data[clamped >> 1] - 239;
    return ((sensitivity * vel_value + 7) >> 3) << 4;
}

static int scale_rate(int midinote, int sensitivity)
{
    int x = imin(31, imax(0, midinote / 3 - 7));
    return (sensitivity * x) >> 3;
}

static const uint8_t exp_scale_data[] = {0,  1,  2,  3,  4,  5,   6,   7,   8,   9,   11,
                                         14, 16, 19, 23, 27, 33,  39,  47,  56,  66,  80,
                                         94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};

static int scale_curve(int group, int depth, int curve)
{
    int scale;
    if (curve == 0 || curve == 3) {
        scale = (group * depth * 329) >> 12;
    } else {
        int n = (int)sizeof exp_scale_data;
        int raw = exp_scale_data[imin(group, n - 1)];
        scale = (raw * depth * 329) >> 15;
    }
    if (curve < 2)
        scale = -scale;
    return scale;
}

static int scale_level(int midinote, int bp, int ld, int rd, int lc, int rc)
{
    int offset = midinote - bp - 17;
    if (offset >= 0)
        return scale_curve(offset / 3, rd, rc);
    return scale_curve((-offset) / 3, ld, lc);
}

static const uint8_t pitchmodsenstab[] = {0, 10, 20, 33, 55, 92, 153, 255};
/* Full depth tremolo per AMS step in log2 Q24, as measured on the FM-1. */
static const int32_t fm1_amp_mod_depth[] = {0, (int32_t)(0.349 * (1 << 24)), (int32_t)(0.880 * (1 << 24)), 14 << 24};

static void setup_pitch(fm_voice_t *v, const uint8_t *patch)
{
    int note = v->midinote + (int)patch[144] - 24;
    int op;
    for (op = 0; op < 6; op++) {
        int off = op * 21;
        v->basepitch[op] = osc_freq(note, patch[off + 17], patch[off + 18], patch[off + 19], patch[off + 20]);
    }
}

static void setup_levels(fm_voice_t *v, const uint8_t *patch, int initial)
{
    int note = v->midinote + (int)patch[144] - 24;
    int op, i;
    for (op = 0; op < 6; op++) {
        int off = op * 21;
        int rates[4], levels[4], outlevel, rate_scaling;
        for (i = 0; i < 4; i++) {
            rates[i] = patch[off + i];
            levels[i] = patch[off + 4 + i];
        }
        outlevel = scaleoutlevel(patch[off + 16]);
        outlevel += scale_level(note, patch[off + 8], patch[off + 9], patch[off + 10], patch[off + 11],
                                patch[off + 12]);
        outlevel = imin(127, outlevel);
        outlevel = outlevel << 5;
        outlevel += scale_velocity(v->velocity, patch[off + 15]);
        outlevel = imax(0, outlevel);
        rate_scaling = scale_rate(note, patch[off + 13]);
        if (initial)
            env_init(&v->env[op], rates, levels, outlevel, rate_scaling);
        else
            env_update(&v->env[op], rates, levels, outlevel, rate_scaling);
        v->ampmodsens[op] = fm1_amp_mod_depth[patch[off + 14] & 3];
    }
}

static void setup_globals(fm_voice_t *v, const uint8_t *patch)
{
    int feedback = patch[135];
    v->algorithm = patch[134];
    v->fb_shift = feedback != 0 ? 8 - feedback : 16;
    /* FM-1 feedback is about a quarter of the msfa strength. */
    if (feedback != 0)
        v->fb_shift += 2;
    v->pitchmoddepth = (patch[139] * 165) >> 6;
    v->ampmoddepth = (patch[140] * 165) >> 6;
    /* Q8: FM-1 vibrato is about 0.895 of the DX7 depth. */
    v->pitchmodsens = pitchmodsenstab[patch[143] & 7] * 229;
    v->op_enable = patch[FM_OP_ENABLE] & 0x3f;
}

void fm_ctrl_init(fm_ctrl_t *c)
{
    c->pitch_bend = 0x2000;
    c->bend_up = 12;
    c->bend_down = 12;
    c->mod_wheel = 0;
    c->aftertouch = 0;
}

void fm_voice_init(fm_voice_t *v, const uint8_t patch[FM_VOICE_SIZE], int midinote, int velocity, int keep_phase)
{
    int rates[4], levels[4], op, i;
    int osc_sync = patch[136] != 0;
    v->midinote = midinote;
    v->velocity = velocity;
    setup_levels(v, patch, 1);
    setup_pitch(v, patch);
    for (op = 0; op < 6; op++) {
        if (osc_sync || !keep_phase)
            v->op[op].phase = 0;
        v->op[op].gain[0] = v->op[op].gain[1] = 0;
    }
    for (i = 0; i < 4; i++) {
        rates[i] = patch[126 + i];
        levels[i] = patch[130 + i];
    }
    penv_set(&v->pitchenv, rates, levels);
    setup_globals(v, patch);
    v->fb_buf[0] = v->fb_buf[1] = 0;
}

void fm_voice_update(fm_voice_t *v, const uint8_t patch[FM_VOICE_SIZE])
{
    int rates[4], levels[4], i;
    setup_levels(v, patch, 0);
    setup_pitch(v, patch);
    for (i = 0; i < 4; i++) {
        rates[i] = patch[126 + i];
        levels[i] = patch[130 + i];
    }
    penv_update(&v->pitchenv, rates, levels);
    setup_globals(v, patch);
}

void fm_voice_transfer(fm_voice_t *v, int midinote)
{
    v->midinote = midinote;
}

void fm_voice_compute(fm_voice_t *v, int32_t *out, int32_t lfo_val, int32_t lfo_delay, const fm_ctrl_t *c)
{
    int32_t pitchmod = penv_sample(&v->pitchenv);
    int pmd = v->pitchmoddepth;
    int amd = v->ampmoddepth;
    int wheel = (c->mod_wheel * 99 * 165) / (127 * 64);
    int at = (c->aftertouch * 99 * 165) / (127 * 64);
    int64_t pmdq, senslfo, ampmod;
    int bend_range;
    int32_t pb;
    int op;

    /* The mod wheel and channel pressure add pitch depth, as on the FM-1. */
    pmd = imin(255, pmd + wheel);
    pmd = imin(255, pmd + at);
    pmdq = mul64(pmd, lfo_delay) >> 8;
    senslfo = mul64(v->pitchmodsens, lfo_val - (1 << 23));
    pitchmod += (int32_t)(mul64(pmdq, senslfo) >> 39);

    bend_range = c->pitch_bend >= 0x2000 ? c->bend_up : c->bend_down;
    pb = (int32_t)(mul64((c->pitch_bend - 0x2000) * bend_range, (1 << 24) / 12) >> 13);
    pitchmod += pb;

    ampmod = mul64(amd, lfo_delay) >> 8;
    ampmod = mul64(ampmod, (1 << 24) - lfo_val) >> 24;

    for (op = 0; op < 6; op++) {
        int32_t level, gain;
        v->op[op].gain[0] = v->op[op].gain[1];
        level = env_sample(&v->env[op]);
        if (v->ampmodsens[op])
            level -= (int32_t)(mul64(ampmod, v->ampmodsens[op]) >> 24);
        v->last_level[op] = level;
        gain = level > 0 ? exp2_lookup(level - (14 * (1 << 24))) : 0;
        if (!(v->op_enable & (1 << (5 - op))))
            gain = 0;
        v->op[op].freq = freq_lookup(v->basepitch[op] + pitchmod);
        v->op[op].gain[1] = gain;
    }
    core_compute(v, out);
}

void fm_voice_keyup(fm_voice_t *v)
{
    int op;
    for (op = 0; op < 6; op++)
        env_keydown(&v->env[op], 0);
    penv_keydown(&v->pitchenv, 0);
}

int fm_voice_finished(const fm_voice_t *v)
{
    int carriers = carrier_mask(v->algorithm);
    int op;
    for (op = 0; op < 6; op++) {
        if (!(carriers & (1 << (5 - op))))
            continue;
        if (v->env[op].ix != 4)
            return 0;
        if (v->last_level[op] > (16 << 16) + (1 << 20))
            return 0;
    }
    return 1;
}
