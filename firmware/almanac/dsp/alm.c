/* How a day becomes an FM-1 voice, effect settings and the notes a key plays, ported from the desktop app's
 * Sources/AlmanacCore/almanac.cc. Keep the two in step: the parity test compares them. */
#include "alm.h"

#include "isc_libc.h"

#include "months.inc"

static int alm_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* As the desktop's rounding: positive and negative halves away from zero. */
static int alm_round_div(int num, int den)
{
    return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}

static int alm_lerp(int lo, int hi, int t) { return lo + alm_round_div((hi - lo) * t, 100); }

/* The reverb's room and decay, the echo's speed and repeats, for each place. */
static const struct {
    const char *name;
    uint8_t room, decay, echo_speed, repeats;
} kPlaces[AL_PLACE_COUNT] = {
    {"Meadow", 0, 35, 70, 15},  {"Forest", 0, 55, 88, 45}, {"Mountains", 1, 72, 12, 45},
    {"Coast", 1, 60, 40, 30},   {"Lake", 2, 76, 30, 20},   {"Village", 2, 40, 62, 10},
};

/* The engine's rates for a pattern's notes: 1/4, 1/8, 1/8T, 1/16, 1/16T. */
static const uint8_t kStepRates[5] = {2, 4, 5, 6, 7};

int alm_month_count(void) { return ALM_MONTH_COUNT; }
const alm_month_t *alm_month(int i) { return &kMonths[alm_clampi(i, 0, ALM_MONTH_COUNT - 1)]; }
const alm_sky_t *alm_sky(int i) { return &kSkies[alm_clampi(i, 0, AL_SKY_COUNT - 1)]; }
const char *alm_place_name(int p) { return kPlaces[alm_clampi(p, 0, AL_PLACE_COUNT - 1)].name; }

const char *alm_mood_name(int mood)
{
    static const char *const names[7] = {"Gloomy", "Wistful", "Pensive", "Calm", "Content", "Glad", "Elated"};
    static const int edges[6] = {16, 31, 46, 56, 71, 86};
    int i = 0;
    while (i < 6 && mood >= edges[i])
        i++;
    return names[i];
}

void alm_default_day(int month, int32_t out[AL_FIELDS])
{
    isc_memcpy(out, alm_month(month)->day, sizeof(int32_t) * AL_FIELDS);
}

void alm_range(int field, int *lo, int *hi)
{
    int a = 0, b = 100;
    switch (field) {
    case AL_MONTH: b = ALM_MONTH_COUNT - 1; break;
    case AL_SKY: b = AL_SKY_COUNT - 1; break;
    case AL_PLACE: b = AL_PLACE_COUNT - 1; break;
    case AL_PLAY: b = AL_PLAY_COUNT - 1; break;
    case AL_VOICING: b = AL_VOICING_COUNT - 1; break;
    case AL_TEMPO: a = 40, b = 200; break;
    case AL_STEP: b = 4; break;
    }
    if (lo)
        *lo = a;
    if (hi)
        *hi = b;
}

void alm_clamp(int32_t s[AL_FIELDS])
{
    int i, lo, hi;
    for (i = 0; i < AL_FIELDS; i++) {
        alm_range(i, &lo, &hi);
        s[i] = alm_clampi(s[i], lo, hi);
    }
}

void alm_voice(const int32_t day[AL_FIELDS], uint8_t v[155])
{
    int32_t s[AL_FIELDS];
    const alm_month_t *m;
    const alm_sky_t *k;
    int mood, warmth, bright, attack, drift, linger, i;
    uint8_t *w;
    isc_memcpy(s, day, sizeof s);
    alm_clamp(s);
    m = alm_month(s[AL_MONTH]);
    k = alm_sky(s[AL_SKY]);
    isc_memcpy(v, m->voice, 155);
    isc_memcpy(v, k->ops, 42);
    v[135] = k->fb;
    v[140] = k->amd;
    v[142] = k->wave;

    mood = s[AL_MOOD] - 50;
    warmth = s[AL_WARMTH] < 50 ? alm_round_div((s[AL_WARMTH] - 50) * 6, 10) : alm_round_div((s[AL_WARMTH] - 50) * 24, 100);
    bright = warmth + alm_round_div(mood * 12, 100);
    attack = alm_round_div(mood * 4, 10);
    drift = mood < 0 ? alm_round_div(-mood * 4, 50) : 0;
    linger = alm_round_div((s[AL_LINGER] - 50) * 5, 10);

    for (i = 0; i < 4; i++) {
        uint8_t *p = v + (5 - i) * 21;
        const uint8_t *o = m->voice + (5 - i) * 21;
        if (m->mod[i])
            p[16] = (uint8_t)alm_clampi(o[16] + bright, 0, 99);
        else
            p[0] = (uint8_t)alm_clampi(o[0] + attack, 1, 99);
        p[20] = (uint8_t)alm_clampi(o[20] + (i < 2 ? -drift : drift), 0, 14);
        p[3] = (uint8_t)alm_clampi(o[3] - linger, 1, 99);
        if (m->decays) {
            p[1] = (uint8_t)alm_clampi(o[1] - linger, 1, 99);
            p[2] = (uint8_t)alm_clampi(o[2] - linger, 1, 99);
        }
    }
    w = v + 21;
    w[16] = (uint8_t)(s[AL_ELEMENTS] == 0 || k->loud == 0 ? 0 : alm_lerp(k->quiet, k->loud, s[AL_ELEMENTS]));
    w[3] = (uint8_t)alm_clampi(k->ops[21 + 3] - linger, 1, 99);
    v[137] = (uint8_t)alm_lerp(k->slow, k->fast, s[AL_GUSTS]);
    v[139] = (uint8_t)alm_clampi(alm_round_div(s[AL_SHIMMER] * 35, 100), 0, 99);
}

/* The filter first, then the effects as a signal would meet them: drive, swirl, chorus, echo, room. */
static const uint8_t kChain[ISC_FX_COUNT] = {ISC_FX_FILTER, ISC_FX_DIST, ISC_FX_PHASER,
                                             ISC_FX_CHORUS, ISC_FX_DELAY, ISC_FX_REVERB};

void alm_fx(const int32_t day[AL_FIELDS], isc_fx_params_t *p)
{
    int32_t s[AL_FIELDS];
    int sk, w, swirl, space;
    int filter_on = 1, filter_type = 0, cutoff = 107, res = 10;
    int chorus_on = 0, chorus_mix = 0, chorus_depth = 50;
    int phaser_on = 0, phaser_mix = 0, phaser_depth = 50;
    int drive_on = 0, drive_gain = 0, drive_tone = 40, drive_level = 70;
    isc_memcpy(s, day, sizeof s);
    alm_clamp(s);
    isc_fx_params_default(p);
    isc_memcpy(p->order, kChain, sizeof kChain);
    sk = s[AL_SKY], w = s[AL_WEATHER];
    swirl = alm_lerp(8, 70, s[AL_GUSTS]);
    switch (sk) {
    case AL_CLEAR: filter_on = 0; break;
    case AL_BREEZE:
        filter_on = 0;
        chorus_on = 1, chorus_mix = alm_lerp(15, 65, w), chorus_depth = alm_lerp(30, 70, w);
        break;
    case AL_RAIN:
        cutoff = alm_lerp(104, 70, w), res = 10;
        chorus_on = 1, chorus_mix = alm_lerp(10, 35, w), chorus_depth = 40;
        break;
    case AL_FOG:
        cutoff = alm_lerp(88, 32, w), res = 5;
        chorus_on = 1, chorus_mix = alm_lerp(15, 40, w), chorus_depth = 70;
        break;
    case AL_STORM:
        cutoff = alm_lerp(104, 80, w), res = 15;
        drive_on = 1, drive_gain = alm_lerp(10, 70, w), drive_level = alm_lerp(82, 66, w);
        phaser_on = 1, phaser_mix = alm_lerp(25, 70, w), phaser_depth = 75;
        break;
    case AL_SNOW:
        filter_type = 2, cutoff = alm_lerp(0, 30, w), res = 15;
        phaser_on = 1, phaser_mix = alm_lerp(15, 45, w), phaser_depth = 40;
        break;
    }
    isc_fx_params_set(p, 1, filter_type);
    isc_fx_params_set(p, 2, cutoff);
    /* Q at the panel's resolution, 0..100, as MF-1's engine takes it. */
    p->cc[3] = (uint8_t)res;
    isc_fx_params_set(p, 0, filter_on);
    isc_fx_params_set(p, 13, drive_gain);
    isc_fx_params_set(p, 14, drive_tone);
    isc_fx_params_set(p, 15, drive_level);
    isc_fx_params_set(p, 12, drive_on);
    isc_fx_params_set(p, 21, swirl);
    isc_fx_params_set(p, 22, phaser_depth);
    isc_fx_params_set(p, 23, phaser_mix);
    isc_fx_params_set(p, 20, phaser_on);
    isc_fx_params_set(p, 17, swirl);
    isc_fx_params_set(p, 18, chorus_depth);
    isc_fx_params_set(p, 19, chorus_mix);
    isc_fx_params_set(p, 16, chorus_on);

    space = s[AL_SPACE] - 50;
    isc_fx_params_set(p, 9, alm_clampi(kPlaces[s[AL_PLACE]].repeats + alm_round_div(space * 6, 10), 0, 90));
    isc_fx_params_set(p, 10, kPlaces[s[AL_PLACE]].echo_speed);
    isc_fx_params_set(p, 11, s[AL_ECHO]);
    isc_fx_params_set(p, 8, s[AL_ECHO] > 0);
    isc_fx_params_set(p, 5, kPlaces[s[AL_PLACE]].room);
    isc_fx_params_set(p, 6, alm_clampi(kPlaces[s[AL_PLACE]].decay + alm_round_div(space * 8, 10), 0, 100));
    isc_fx_params_set(p, 7, s[AL_DISTANCE]);
    isc_fx_params_set(p, 4, s[AL_DISTANCE] > 0);
}

void alm_pattern(const int32_t day[AL_FIELDS], alm_pattern_t *out)
{
    int32_t s[AL_FIELDS];
    const alm_month_t *m;
    isc_memcpy(s, day, sizeof s);
    alm_clamp(s);
    m = alm_month(s[AL_MONTH]);
    out->on = s[AL_PLAY] == AL_PATTERN;
    out->mode = m->arp_mode;
    out->octaves = m->arp_octaves + (s[AL_VOICING] == AL_WIDE);
    out->rate = kStepRates[s[AL_STEP]];
    out->gate = 80;
    out->tempo = s[AL_TEMPO];
}

int alm_key_notes(const int32_t day[AL_FIELDS], int note, int left, int out[8])
{
    int32_t s[AL_FIELDS];
    const alm_month_t *m;
    int n = 0, i, voicing;
#define ADD(x)                                                                                                 \
    do {                                                                                                       \
        int x_ = (x), j_, dup_ = 0;                                                                            \
        for (j_ = 0; j_ < n; j_++)                                                                             \
            dup_ |= out[j_] == x_;                                                                             \
        if (x_ >= 0 && x_ <= 127 && n < 8 && !dup_)                                                            \
            out[n++] = x_;                                                                                     \
    } while (0)
    isc_memcpy(s, day, sizeof s);
    alm_clamp(s);
    m = alm_month(s[AL_MONTH]);
    voicing = s[AL_VOICING];
    switch (s[AL_PLAY]) {
    case AL_SPLIT:
        if (!left) {
            ADD(note);
            break;
        }
        /* The left hand holds a drone: the root an octave down, then its fifth, then the octave below. */
        ADD(note - 12);
        if (voicing >= AL_OPEN)
            ADD(note - 5);
        if (voicing == AL_WIDE)
            ADD(note - 24);
        break;
    case AL_CHORD:
    case AL_PATTERN:
        if (voicing == AL_WIDE)
            ADD(note - 12);
        /* Open voicing lifts the second note of the chord by an octave. */
        for (i = 0; i < m->chord_size; i++)
            ADD(note + m->chord[i] + (voicing != AL_CLOSE && i == 1 ? 12 : 0));
        break;
    default: ADD(note);
    }
#undef ADD
    return n;
}
