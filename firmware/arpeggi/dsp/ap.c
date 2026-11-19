/* How a scene's settings become an FM-1 voice and effect settings, ported from the desktop app's
 * Sources/ArpeggiCore/arpeggi.cc. Keep the two in step: the parity test compares them. */
#include "ap.h"

#include "isc_libc.h"

#include "scenes.inc"

static int ap_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* As the desktop's rounding: positive and negative halves away from zero. */
static int ap_round_div(int num, int den)
{
    return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}

static const char *const kShapes[AP_SHAPE_COUNT] = {"Up",     "Down", "Bounce",  "Order", "Converge", "Thumb",
                                                    "Spiral", "Walk", "Shuffle", "Chord", "Strum"};
static const char *const kRates[AP_RATE_COUNT] = {"1/2",  "1/4",   "1/4.",  "1/8",  "1/8.", "1/8T",
                                                  "1/16", "1/16.", "1/16T", "1/20", "1/28", "1/32"};
static const char *const kRhythms[AP_RHYTHM_COUNT] = {"Every",   "7 of 8", "5 of 8", "3 of 8", "5 of 12",
                                                      "Ratchet", "Gallop", "Skip",   "Fall"};
static const char *const kRelations[AP_RELATION_COUNT] = {"Together", "Hocket", "Answer", "Canon", "Mirror", "Trade"};
static const char *const kHolds[AP_HOLD_COUNT] = {"Hold", "Latch", "Ring"};
static const char *const kSoundNames[AP_SOUND_COUNT] = {"Pluck", "Glass", "Mallet", "Pad",
                                                        "Chip",  "Bass",  "Bell",   "Keys"};

int ap_scene_count(void) { return AP_SCENE_COUNT; }
const ap_scene_t *ap_scene(int i) { return &kScenes[ap_clampi(i, 0, AP_SCENE_COUNT - 1)]; }
const char *ap_shape_name(int i) { return kShapes[ap_clampi(i, 0, AP_SHAPE_COUNT - 1)]; }
const char *ap_rate_name(int i) { return kRates[ap_clampi(i, 0, AP_RATE_COUNT - 1)]; }
const char *ap_rhythm_name(int i) { return kRhythms[ap_clampi(i, 0, AP_RHYTHM_COUNT - 1)]; }
const char *ap_relation_name(int i) { return kRelations[ap_clampi(i, 0, AP_RELATION_COUNT - 1)]; }
const char *ap_hold_name(int i) { return kHolds[ap_clampi(i, 0, AP_HOLD_COUNT - 1)]; }
const char *ap_sound_name(int i) { return kSoundNames[ap_clampi(i, 0, AP_SOUND_COUNT - 1)]; }

void ap_default_scene(int scene, int32_t out[AP_FIELDS])
{
    scene = ap_clampi(scene, 0, AP_SCENE_COUNT - 1);
    isc_memcpy(out, kScenes[scene].s, sizeof(int32_t) * AP_FIELDS);
    out[AP_SCENE] = scene;
}

int ap_default_seed(int scene, int8_t out[AP_MAX_POOL])
{
    const ap_scene_t *c = ap_scene(scene);
    int n = c->seed_len < AP_MAX_POOL ? c->seed_len : AP_MAX_POOL, i;
    for (i = 0; i < n; i++)
        out[i] = c->seed[i];
    return n;
}

void ap_range(int field, int *lo, int *hi)
{
    int a = 0, b = 100;
    switch (field) {
    case AP_SCENE: b = AP_SCENE_COUNT - 1; break;
    case AP_RELATION: b = AP_RELATION_COUNT - 1; break;
    case AP_DUO: b = 1; break;
    case AP_A_SHAPE:
    case AP_B_SHAPE: b = AP_SHAPE_COUNT - 1; break;
    case AP_A_RATE:
    case AP_B_RATE: b = AP_RATE_COUNT - 1; break;
    case AP_A_RHYTHM:
    case AP_B_RHYTHM: b = AP_RHYTHM_COUNT - 1; break;
    case AP_A_GATE:
    case AP_B_GATE: a = 5, b = 200; break;
    case AP_HOLD_MODE: b = AP_HOLD_COUNT - 1; break;
    case AP_RING_SIZE: a = 1, b = 8; break;
    case AP_SPAN: a = 1, b = AP_MAX_SPAN; break;
    case AP_B_PITCH: a = -24, b = 24; break;
    case AP_SOUND: b = AP_SOUND_COUNT - 1; break;
    case AP_TEMPO: a = 40, b = 240; break;
    case AP_ECHO_STEPS: a = 1, b = 8; break;
    }
    if (lo)
        *lo = a;
    if (hi)
        *hi = b;
}

void ap_clamp(int32_t s[AP_FIELDS])
{
    int i, lo, hi;
    for (i = 0; i < AP_FIELDS; i++) {
        ap_range(i, &lo, &hi);
        s[i] = ap_clampi(s[i], lo, hi);
    }
}

/* A ratio times 100 as the DX7 writes it: coarse 0 is a half, and fine adds hundredths of the coarse ratio. */
static void ap_ratio_at(uint8_t *p, int ratio)
{
    int coarse = ratio < 100 ? 0 : ratio / 100 < 31 ? ratio / 100 : 31;
    int fine = coarse == 0 ? ap_round_div((ratio - 50) * 100, 50)
                           : ap_round_div((ratio - coarse * 100) * 100, coarse * 100);
    p[17] = 0, p[18] = (uint8_t)coarse, p[19] = (uint8_t)ap_clampi(fine, 0, 99);
}

void ap_voice(const int32_t settings[AP_FIELDS], uint8_t v[155])
{
    int32_t s[AP_FIELDS];
    const ap_sound_t *snd;
    const char *name;
    int bright, decay, k, i, n;
    isc_memcpy(s, settings, sizeof s);
    ap_clamp(s);
    snd = &kSounds[s[AP_SOUND]];
    isc_memset(v, 0, 155);
    bright = ap_round_div((s[AP_BRIGHT] - 50) * 14, 50);
    decay = s[AP_DECAY] - 50;
    for (k = 0; k < 6; k++) {
        const ap_op_t *o = &snd->op[k];
        uint8_t *p = v + (5 - k) * 21; /* VCED stores OP6 first */
        p[0] = (uint8_t)ap_clampi(o->r[0], 1, 99);
        p[1] = (uint8_t)ap_clampi(o->r[1] - ap_round_div(decay * 16, 50), 1, 99);
        p[2] = (uint8_t)ap_clampi(o->r[2] - ap_round_div(decay * 16, 50), 1, 99);
        p[3] = (uint8_t)ap_clampi(o->r[3] - ap_round_div(decay * 12, 50), 1, 99);
        for (i = 0; i < 4; i++)
            p[4 + i] = (uint8_t)ap_clampi(o->l[i], 0, 99);
        p[8] = 39;
        p[13] = (uint8_t)ap_clampi(o->rate_scale, 0, 7);
        p[14] = (uint8_t)ap_clampi(o->ams, 0, 3);
        p[16] = (uint8_t)ap_clampi(o->level + (o->bright && o->level > 0 ? bright : 0), 0, 99);
        ap_ratio_at(p, o->ratio);
        p[20] = (uint8_t)ap_clampi(o->detune, 0, 14);
    }
    for (k = 0; k < 4; k++)
        v[126 + k] = 99, v[130 + k] = 50;
    v[134] = (uint8_t)(snd->algorithm - 1);
    v[135] = (uint8_t)ap_clampi(snd->feedback, 0, 7);
    v[136] = 1; /* oscillator key sync: every strike starts the same */
    v[137] = snd->lfo_speed;
    v[138] = snd->lfo_delay;
    v[139] = snd->pmd;
    v[140] = snd->amd;
    v[141] = snd->lfo_sync ? 1 : 0;
    v[142] = snd->lfo_wave;
    v[143] = snd->pms;
    v[144] = (uint8_t)ap_clampi(24 + snd->transpose, 0, 48);
    name = kScenes[s[AP_SCENE]].voice_name;
    for (n = 0; name[n]; n++)
        ;
    for (i = 0; i < 10; i++)
        v[145 + i] = (uint8_t)(i < n ? name[i] : ' ');
}

/* The filter first, then the effects as a signal would meet them: drive, swirl, chorus, echo, room. */
static const uint8_t kChain[ISC_FX_COUNT] = {ISC_FX_FILTER, ISC_FX_DIST, ISC_FX_PHASER,
                                             ISC_FX_CHORUS, ISC_FX_DELAY, ISC_FX_REVERB};

void ap_fx(const int32_t settings[AP_FIELDS], isc_fx_params_t *p)
{
    int32_t s[AP_FIELDS];
    int steps, tempo, chorus;
    isc_memcpy(s, settings, sizeof s);
    ap_clamp(s);
    chorus = kSounds[s[AP_SOUND]].chorus;
    isc_fx_params_default(p);
    isc_memcpy(p->order, kChain, sizeof kChain);
    /* No filter, drive or phaser: the sound is the voice's own, with the chorus some sounds ask for. */
    isc_fx_params_set(p, 0, 0);
    isc_fx_params_set(p, 12, 0);
    isc_fx_params_set(p, 20, 0);
    isc_fx_params_set(p, 17, 20);
    isc_fx_params_set(p, 18, 55);
    isc_fx_params_set(p, 19, chorus);
    isc_fx_params_set(p, 16, chorus > 0 ? 1 : 0);

    /* Room: the reverb's mix, with a longer tail as it grows. */
    isc_fx_params_set(p, 5, kSounds[s[AP_SOUND]].room_type);
    isc_fx_params_set(p, 6, ap_clampi(24 + s[AP_ROOM] * 3 / 5, 0, 100));
    isc_fx_params_set(p, 7, ap_round_div(s[AP_ROOM] * 70, 100));
    isc_fx_params_set(p, 4, s[AP_ROOM] > 0 ? 1 : 0);

    /* Echo: a whole number of sixteenths at the tempo, as near as the delay's 0.1 to 0.8 s reach allows. A sixteenth
     * is 15/tempo seconds; the desktop's arithmetic in doubles comes out the same as these exact fractions. */
    tempo = s[AP_TEMPO];
    steps = s[AP_ECHO_STEPS];
    while (steps > 1 && 75 * steps > 4 * tempo)
        steps--;
    while (steps < 64 && 150 * steps < tempo)
        steps++;
    /* The delay's time knob runs from 0.8 s at 0 to 0.1 s at 100, rounded half up. */
    isc_fx_params_set(p, 10, ap_clampi((1607 * tempo - 30000 * steps) / (14 * tempo), 0, 100));
    isc_fx_params_set(p, 9, ap_clampi(20 + s[AP_ECHO] / 2, 0, 70));
    isc_fx_params_set(p, 11, s[AP_ECHO]);
    isc_fx_params_set(p, 8, s[AP_ECHO] > 0 ? 1 : 0);
}
