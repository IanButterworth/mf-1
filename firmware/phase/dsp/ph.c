/* How a piece's settings become an FM-1 voice and effect settings, ported from the desktop app's
 * Sources/PhaseCore/phase.cc. Keep the two in step: the parity test compares them. */
#include "ph.h"

#include "isc_libc.h"

#include "pieces.inc"

static int ph_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* As the desktop's rounding: positive and negative halves away from zero. */
static int ph_round_div(int num, int den)
{
    return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}

static const char *const kProcesses[PH_PROCESS_COUNT] = {"Phase", "Shift", "Build", "Canon",
                                                         "Pulse", "Stretch", "Result", "Tape"};
static const char *const kSoundNames[PH_SOUND_COUNT] = {"Marimba", "Vibes", "Glock", "Piano",
                                                        "Organ",   "Wood",  "Voice", "Bell"};
static const char *const kSpreads[PH_SPREAD_COUNT] = {"Unison", "Close", "Open", "Wide"};

int ph_piece_count(void) { return PH_PIECE_COUNT; }
const ph_piece_t *ph_piece(int i) { return &kPieces[ph_clampi(i, 0, PH_PIECE_COUNT - 1)]; }
const char *ph_process_name(int p) { return kProcesses[ph_clampi(p, 0, PH_PROCESS_COUNT - 1)]; }
const char *ph_sound_name(int s) { return kSoundNames[ph_clampi(s, 0, PH_SOUND_COUNT - 1)]; }
const char *ph_spread_name(int s) { return kSpreads[ph_clampi(s, 0, PH_SPREAD_COUNT - 1)]; }

void ph_default_piece(int piece, int32_t out[PH_FIELDS])
{
    piece = ph_clampi(piece, 0, PH_PIECE_COUNT - 1);
    isc_memcpy(out, kPieces[piece].s, sizeof(int32_t) * PH_FIELDS);
    out[PH_PIECE] = piece;
}

void ph_default_cell(int piece, PhaseCell *out)
{
    const ph_piece_t *p = ph_piece(piece);
    int i;
    isc_memset(out->note, -1, sizeof out->note);
    out->length = (int8_t)(p->cell_len < PH_MAX_STEPS ? p->cell_len : PH_MAX_STEPS);
    for (i = 0; i < out->length; i++)
        out->note[i] = p->cell[i];
}

void ph_range(int field, int *lo, int *hi)
{
    int a = 0, b = 100;
    switch (field) {
    case PH_PIECE: b = PH_PIECE_COUNT - 1; break;
    case PH_PROCESS: b = PH_PROCESS_COUNT - 1; break;
    case PH_PARTS: a = 1, b = PH_MAX_PARTS; break;
    case PH_REPEATS: a = 1, b = 16; break;
    case PH_LENGTH: a = 2, b = PH_MAX_STEPS; break;
    case PH_TRANSPOSE: a = -12, b = 12; break;
    case PH_GATE: a = 5; break;
    case PH_SOUND: b = PH_SOUND_COUNT - 1; break;
    case PH_TEMPO: a = 40, b = 200; break;
    case PH_ECHO_STEPS: a = 1, b = 8; break;
    case PH_SPREAD: b = PH_SPREAD_COUNT - 1; break;
    }
    if (lo)
        *lo = a;
    if (hi)
        *hi = b;
}

void ph_clamp(int32_t s[PH_FIELDS])
{
    int i, lo, hi;
    for (i = 0; i < PH_FIELDS; i++) {
        ph_range(i, &lo, &hi);
        s[i] = ph_clampi(s[i], lo, hi);
    }
}

/* A ratio times 100 as the DX7 writes it: coarse 0 is a half, and fine adds hundredths of the coarse ratio. */
static void ph_ratio_at(uint8_t *p, int ratio)
{
    int coarse = ratio < 100 ? 0 : ratio / 100 < 31 ? ratio / 100 : 31;
    int fine = coarse == 0 ? ph_round_div((ratio - 50) * 100, 50)
                           : ph_round_div((ratio - coarse * 100) * 100, coarse * 100);
    p[17] = 0, p[18] = (uint8_t)coarse, p[19] = (uint8_t)ph_clampi(fine, 0, 99);
}

void ph_voice(const int32_t settings[PH_FIELDS], uint8_t v[155])
{
    int32_t s[PH_FIELDS];
    const ph_sound_t *snd;
    const char *name;
    int bright, decay, k, i, n;
    isc_memcpy(s, settings, sizeof s);
    ph_clamp(s);
    snd = &kSounds[s[PH_SOUND]];
    isc_memset(v, 0, 155);
    bright = ph_round_div((s[PH_BRIGHT] - 50) * 14, 50);
    decay = s[PH_DECAY] - 50;
    for (k = 0; k < 6; k++) {
        const ph_op_t *o = &snd->op[k];
        uint8_t *p = v + (5 - k) * 21; /* VCED stores OP6 first */
        p[0] = (uint8_t)ph_clampi(o->r[0], 1, 99);
        p[1] = (uint8_t)ph_clampi(o->r[1] - ph_round_div(decay * 16, 50), 1, 99);
        p[2] = (uint8_t)ph_clampi(o->r[2] - ph_round_div(decay * 16, 50), 1, 99);
        p[3] = (uint8_t)ph_clampi(o->r[3] - ph_round_div(decay * 12, 50), 1, 99);
        for (i = 0; i < 4; i++)
            p[4 + i] = (uint8_t)ph_clampi(o->l[i], 0, 99);
        p[8] = 39;
        p[13] = (uint8_t)ph_clampi(o->rate_scale, 0, 7);
        p[14] = (uint8_t)ph_clampi(o->ams, 0, 3);
        p[16] = (uint8_t)ph_clampi(o->level + (o->bright && o->level > 0 ? bright : 0), 0, 99);
        ph_ratio_at(p, o->ratio);
        p[20] = (uint8_t)ph_clampi(o->detune, 0, 14);
    }
    for (k = 0; k < 4; k++)
        v[126 + k] = 99, v[130 + k] = 50;
    v[134] = (uint8_t)(snd->algorithm - 1);
    v[135] = (uint8_t)ph_clampi(snd->feedback, 0, 7);
    v[136] = 1; /* oscillator key sync: every stroke starts the same */
    v[137] = snd->lfo_speed;
    v[138] = snd->lfo_delay;
    v[139] = snd->pmd;
    v[140] = snd->amd;
    v[141] = snd->lfo_sync ? 1 : 0;
    v[142] = snd->lfo_wave;
    v[143] = snd->pms;
    v[144] = (uint8_t)ph_clampi(24 + snd->transpose, 0, 48);
    name = kPieces[s[PH_PIECE]].voice_name;
    for (n = 0; name[n]; n++)
        ;
    for (i = 0; i < 10; i++)
        v[145 + i] = (uint8_t)(i < n ? name[i] : ' ');
}

/* The filter first, then the effects as a signal would meet them: drive, swirl, chorus, echo, room. */
static const uint8_t kChain[ISC_FX_COUNT] = {ISC_FX_FILTER, ISC_FX_DIST, ISC_FX_PHASER,
                                             ISC_FX_CHORUS, ISC_FX_DELAY, ISC_FX_REVERB};

void ph_fx(const int32_t settings[PH_FIELDS], isc_fx_params_t *p)
{
    int32_t s[PH_FIELDS];
    int steps, tempo;
    isc_memcpy(s, settings, sizeof s);
    ph_clamp(s);
    isc_fx_params_default(p);
    isc_memcpy(p->order, kChain, sizeof kChain);
    /* The sound is the voice's own: no filter, drive, phaser or chorus. */
    isc_fx_params_set(p, 0, 0);
    isc_fx_params_set(p, 12, 0);
    isc_fx_params_set(p, 20, 0);
    isc_fx_params_set(p, 16, 0);

    /* Room: the reverb's mix, with a longer tail as it grows. */
    isc_fx_params_set(p, 5, kSounds[s[PH_SOUND]].room_type);
    isc_fx_params_set(p, 6, ph_clampi(24 + s[PH_ROOM] * 2 / 5, 0, 100));
    isc_fx_params_set(p, 7, ph_round_div(s[PH_ROOM] * 70, 100));
    isc_fx_params_set(p, 4, s[PH_ROOM] > 0 ? 1 : 0);

    /* Echo: a whole number of steps at the tempo, as near as the delay's 0.1 to 0.8 s reach allows. A step is 15/tempo
     * seconds; the desktop's arithmetic in doubles comes out the same as these exact fractions. */
    tempo = s[PH_TEMPO];
    steps = s[PH_ECHO_STEPS];
    while (steps > 1 && 75 * steps > 4 * tempo)
        steps--;
    while (steps < 64 && 150 * steps < tempo)
        steps++;
    /* The delay's time knob runs from 0.8 s at 0 to 0.1 s at 100, rounded half up. */
    isc_fx_params_set(p, 10, ph_clampi((1607 * tempo - 30000 * steps) / (14 * tempo), 0, 100));
    isc_fx_params_set(p, 9, 30);
    isc_fx_params_set(p, 11, s[PH_ECHO]);
    isc_fx_params_set(p, 8, s[PH_ECHO] > 0 ? 1 : 0);
}
