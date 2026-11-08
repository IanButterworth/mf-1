/* How a dog's settings become an FM-1 bark, effect settings and the notes a key plays, ported from the desktop
 * app's Sources/WoofCore/woof.cc. Keep the two in step: the parity test compares them. */
#include "wf.h"

#include "isc_libc.h"

#include "dogs.inc"

static int wf_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int wf_maxi(int a, int b) { return a > b ? a : b; }

/* As the desktop's rounding: positive and negative halves away from zero. */
static int wf_round_div(int num, int den)
{
    return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}

static int wf_lerp(int lo, int hi, int t) { return lo + wf_round_div((hi - lo) * t, 100); }

/* The reverb's room and decay, how much of Distance reaches it, the echo's speed and repeats, for each place.
 * Outdoors there is no reverb tail, only a short reflection. */
static const struct {
    const char *name;
    uint8_t room, decay, wet, echo_speed, repeats;
} kPlaces[WF_PLACE_COUNT] = {
    {"Park", 0, 6, 33, 70, 10},     {"Parlour", 0, 20, 100, 85, 5}, {"Rooftops", 0, 10, 33, 14, 50},
    {"Farm", 0, 8, 33, 40, 25},     {"River", 0, 6, 33, 28, 25},
};

/* The engine's rates for repeats: 1/4, 1/8, 1/8T, 1/16, 1/16T. */
static const uint8_t kStepRates[5] = {2, 4, 5, 6, 7};

/* The pack: semitones from the key. */
static const int8_t kPack[WF_PACK_COUNT][5] = {{0, 7}, {0, 7, -5}, {0, 7, 12, 16, 19}};
static const uint8_t kPackSize[WF_PACK_COUNT] = {2, 3, 5};

int wf_dog_count(void) { return WF_DOG_COUNT; }
const wf_breed_t *wf_breed(int i) { return &kBreeds[wf_clampi(i, 0, WF_DOG_COUNT - 1)]; }
const char *wf_place_name(int p) { return kPlaces[wf_clampi(p, 0, WF_PLACE_COUNT - 1)].name; }

const char *wf_mood_name(int mood)
{
    static const char *const names[7] = {"Mopey", "Sulky", "Sleepy", "Calm", "Happy", "Excited", "Zoomies"};
    static const int edges[6] = {16, 31, 46, 56, 71, 86};
    int i = 0;
    while (i < 6 && mood >= edges[i])
        i++;
    return names[i];
}

void wf_default_dog(int dog, int32_t out[WF_FIELDS])
{
    dog = wf_clampi(dog, 0, WF_DOG_COUNT - 1);
    isc_memcpy(out, kBreeds[dog].dog, sizeof(int32_t) * WF_FIELDS);
    out[WF_DOG] = dog;
}

void wf_range(int field, int *lo, int *hi)
{
    int a = 0, b = 100;
    switch (field) {
    case WF_DOG: b = WF_DOG_COUNT - 1; break;
    case WF_PLACE: b = WF_PLACE_COUNT - 1; break;
    case WF_PLAY: b = WF_PLAY_COUNT - 1; break;
    case WF_PACK: b = WF_PACK_COUNT - 1; break;
    case WF_TEMPO: a = 40, b = 200; break;
    case WF_STEP: b = 4; break;
    }
    if (lo)
        *lo = a;
    if (hi)
        *hi = b;
}

void wf_clamp(int32_t s[WF_FIELDS])
{
    int i, lo, hi;
    for (i = 0; i < WF_FIELDS; i++) {
        wf_range(i, &lo, &hi);
        s[i] = wf_clampi(s[i], lo, hi);
    }
}

/* One operator's 21 VCED bytes. */
static void wf_op(uint8_t *p, int r1, int r2, int r3, int r4, int l1, int l2, int l3, int l4, int level)
{
    p[0] = (uint8_t)wf_clampi(r1, 1, 99), p[1] = (uint8_t)wf_clampi(r2, 1, 99);
    p[2] = (uint8_t)wf_clampi(r3, 1, 99), p[3] = (uint8_t)wf_clampi(r4, 1, 99);
    p[4] = (uint8_t)wf_clampi(l1, 0, 99), p[5] = (uint8_t)wf_clampi(l2, 0, 99);
    p[6] = (uint8_t)wf_clampi(l3, 0, 99), p[7] = (uint8_t)wf_clampi(l4, 0, 99);
    p[8] = 39;
    p[15] = 2;
    p[16] = (uint8_t)wf_clampi(level, 0, 99);
    p[18] = 1;
    p[20] = 7;
}

static void wf_fixed_at(uint8_t *p, int f)
{
    f = wf_clampi(f, 0, 399);
    p[17] = 1, p[18] = (uint8_t)(f / 100), p[19] = (uint8_t)(f % 100);
}

void wf_voice(const int32_t dog[WF_FIELDS], uint8_t v[155])
{
    int32_t s[WF_FIELDS];
    const wf_breed_t *b;
    int size, gruff, length, yelp, mood, howl, growl, breath, shift, bright;
    int attack, hold, stop, held, sustain, release, rise, fall, i, n;
    uint8_t *throat_c, *throat_m, *mouth_c, *mouth_m, *breath_c, *breath_m, *peg;
    isc_memcpy(s, dog, sizeof s);
    wf_clamp(s);
    b = wf_breed(s[WF_DOG]);
    isc_memset(v, 0, 155);

    size = s[WF_SIZE] - 50, gruff = s[WF_GRUFF] - 50, length = s[WF_LENGTH] - 50;
    yelp = s[WF_YELP] - 50, mood = s[WF_MOOD] - 50;
    howl = s[WF_HOWL], growl = s[WF_GROWL], breath = s[WF_BREATH];
    shift = -wf_round_div(size * 16, 50);
    bright = wf_round_div(mood * 6, 50);

    attack = 72 + (mood > 0 ? wf_round_div(mood * 8, 50) : wf_round_div(mood * 24, 50)) - wf_round_div(howl * 26, 100);
    hold = wf_clampi(b->decay - wf_round_div(length * 25, 50) - wf_round_div(howl * 20, 100), 1, 99);
    stop = wf_lerp(62, 40, howl);
    held = wf_lerp(86, 96, howl);
    sustain = wf_round_div(howl * 90, 100);
    release = wf_lerp(62, 34, howl);
    throat_c = v + 5 * 21; /* VCED stores OP6 first */
    throat_m = v + 4 * 21;
    mouth_c = v + 3 * 21;
    mouth_m = v + 2 * 21;
    breath_c = v + 1 * 21;
    breath_m = v;
    wf_op(throat_c, attack, hold, stop, release, 99, held, wf_maxi(sustain, wf_round_div(growl * 82, 100)), 0, 99);
    wf_op(throat_m, attack + 4, hold + 4, stop, release, 99, held - 4, wf_maxi(sustain - 10, wf_round_div(growl * 70, 100)),
          0, b->throat + bright + wf_round_div(gruff * 10, 50));
    wf_op(mouth_c, attack - 6, hold, stop, release, 99, held, sustain, 0, b->mouth_level);
    wf_fixed_at(mouth_c, b->formant + shift);
    wf_op(mouth_m, attack, hold + 2, stop, release, 99, held - 3, wf_maxi(0, sustain - 8), 0, b->mouth + bright);
    wf_op(breath_c, attack + 6, hold + 8, stop, release, 99, held - 8, wf_round_div(sustain * 6, 10), 0,
          breath == 0 ? 0 : wf_clampi(b->breath + wf_round_div((breath - 50) * 22, 50), 0, 99));
    wf_fixed_at(breath_c, b->rasp + shift);
    wf_op(breath_m, 99, 99, 99, 99, 99, 99, 99, 0, wf_clampi(76 + wf_round_div(gruff * 16, 50), 0, 99));
    wf_fixed_at(breath_m, b->rasp + shift - 15);
    throat_c[14] = mouth_c[14] = (uint8_t)(growl == 0 ? 0 : growl < 50 ? 2 : 3);

    rise = wf_clampi(b->rise + wf_round_div(yelp * 12, 50) + wf_round_div(mood * 4, 50) + wf_round_div(howl * 6, 100), -10, 40);
    fall = b->fall + (mood < 0 ? wf_round_div(mood * 6, 50) : 0) - wf_round_div(growl * 6, 100);
    peg = v + 126;
    peg[0] = (uint8_t)wf_clampi(90 - wf_round_div(howl * 36, 100), 1, 99);
    peg[1] = (uint8_t)wf_clampi(64 - wf_round_div(howl * 26, 100), 1, 99);
    peg[2] = 50;
    peg[3] = 50;
    peg[4] = (uint8_t)wf_clampi(50 + rise, 0, 99);
    peg[5] = (uint8_t)wf_clampi(50 + fall, 0, 99);
    peg[6] = (uint8_t)wf_clampi(50 + fall, 0, 99);
    peg[7] = (uint8_t)wf_clampi(50 + b->start, 0, 99);

    v[134] = 4;
    v[135] = (uint8_t)wf_clampi(b->gruff + wf_round_div(gruff * 4, 50), 0, 7);
    v[136] = 1;
    if (growl > 0) {
        v[137] = (uint8_t)wf_lerp(72, 92, growl);
        v[138] = 0;
        v[139] = (uint8_t)wf_round_div(growl * 14, 100);
        v[140] = (uint8_t)wf_round_div(growl * 60, 100);
        v[142] = 5;
        v[143] = 3;
    } else {
        v[137] = 34;
        v[138] = 45;
        v[139] = (uint8_t)wf_round_div(howl * 10, 100);
        v[140] = 0;
        v[142] = 4;
        v[143] = 3;
    }
    v[141] = 1;
    v[144] = (uint8_t)wf_clampi(24 + b->transpose - wf_round_div(size * 12, 50), 0, 48);
    for (n = 0; b->voice_name[n]; n++)
        ;
    for (i = 0; i < 10; i++)
        v[145 + i] = (uint8_t)(i < n ? b->voice_name[i] : ' ');
}

/* The filter first, then the effects as a signal would meet them: drive, swirl, chorus, echo, room. */
static const uint8_t kChain[ISC_FX_COUNT] = {ISC_FX_FILTER, ISC_FX_DIST, ISC_FX_PHASER,
                                             ISC_FX_CHORUS, ISC_FX_DELAY, ISC_FX_REVERB};

void wf_fx(const int32_t dog[WF_FIELDS], isc_fx_params_t *p)
{
    int32_t s[WF_FIELDS];
    int rough, space;
    isc_memcpy(s, dog, sizeof s);
    wf_clamp(s);
    isc_fx_params_default(p);
    isc_memcpy(p->order, kChain, sizeof kChain);
    rough = wf_maxi(s[WF_GRUFF] - 50, 0) * 2 + s[WF_GROWL] / 2;
    isc_fx_params_set(p, 0, 0);
    isc_fx_params_set(p, 13, wf_clampi(rough * 6 / 10, 0, 70));
    isc_fx_params_set(p, 14, 45);
    isc_fx_params_set(p, 15, wf_clampi(80 - rough / 6, 60, 80));
    isc_fx_params_set(p, 12, rough > 0);
    isc_fx_params_set(p, 20, 0);
    isc_fx_params_set(p, 16, 0);

    space = s[WF_SPACE] - 50;
    isc_fx_params_set(p, 9, wf_clampi(kPlaces[s[WF_PLACE]].repeats + wf_round_div(space * 6, 10), 0, 90));
    isc_fx_params_set(p, 10, kPlaces[s[WF_PLACE]].echo_speed);
    isc_fx_params_set(p, 11, s[WF_ECHO]);
    isc_fx_params_set(p, 8, s[WF_ECHO] > 0);
    isc_fx_params_set(p, 5, kPlaces[s[WF_PLACE]].room);
    isc_fx_params_set(p, 6, wf_clampi(kPlaces[s[WF_PLACE]].decay + wf_round_div(space * 8, 10), 0, 100));
    isc_fx_params_set(p, 7, wf_round_div(s[WF_DISTANCE] * kPlaces[s[WF_PLACE]].wet, 100));
    isc_fx_params_set(p, 4, s[WF_DISTANCE] > 0);
}

void wf_pattern(const int32_t dog[WF_FIELDS], wf_pattern_t *out)
{
    int32_t s[WF_FIELDS];
    isc_memcpy(s, dog, sizeof s);
    wf_clamp(s);
    out->on = s[WF_PLAY] == WF_REPEAT || s[WF_PLAY] == WF_CHASE;
    out->mode = s[WF_PLAY] == WF_CHASE ? wf_breed(s[WF_DOG])->arp_mode : 5;
    out->octaves = 1;
    out->rate = kStepRates[s[WF_STEP]];
    out->gate = 50;
    out->tempo = s[WF_TEMPO];
}

int wf_key_notes(const int32_t dog[WF_FIELDS], int note, int out[8])
{
    int32_t s[WF_FIELDS];
    int n = 0, i, j, x, dup;
    isc_memcpy(s, dog, sizeof s);
    wf_clamp(s);
    if (s[WF_PLAY] == WF_CHORUS || s[WF_PLAY] == WF_CHASE) {
        for (i = 0; i < kPackSize[s[WF_PACK]]; i++) {
            x = note + kPack[s[WF_PACK]][i];
            for (j = 0, dup = 0; j < n; j++)
                dup |= out[j] == x;
            if (x >= 0 && x <= 127 && n < 8 && !dup)
                out[n++] = x;
        }
    } else if (note >= 0 && note <= 127) {
        out[n++] = note;
    }
    return n;
}
