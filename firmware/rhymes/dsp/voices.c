/* Rhymes' sounds: one DX7 voice for each song, made of classic FM recipes (mallets, bells, a music box, a flute, a
 * banjo, a calliope), and the FM-1 effects they are heard through. Plain C with no C library, as the FM-1 firmware
 * builds it too. */
#include "RhymesCore.h"
#include "internal.h"

/* One operator's 21 VCED bytes; n is 1..6, and VCED stores OP6 first. Rates and levels as the DX7 has them;
 * coarse 0 is half the key's frequency, and the fine value adds that percent of the coarse. */
static void op(uint8_t *v, int n, int coarse, int fine, int level, int r1, int r2, int r3, int r4, int l1, int l2,
               int l3, int l4, int rs)
{
    uint8_t *p = v + (6 - n) * 21;
    p[0] = (uint8_t)r1, p[1] = (uint8_t)r2, p[2] = (uint8_t)r3, p[3] = (uint8_t)r4;
    p[4] = (uint8_t)l1, p[5] = (uint8_t)l2, p[6] = (uint8_t)l3, p[7] = (uint8_t)l4;
    p[8] = 39;
    p[13] = (uint8_t)rs;  /* higher notes die away sooner, as on a real mallet or string */
    p[15] = 2;
    p[16] = (uint8_t)level;
    p[18] = (uint8_t)coarse;
    p[19] = (uint8_t)fine;
    p[20] = 7;
}

/* A struck sound: full at once, then dying away at rate r2 to nothing. */
static void strike(uint8_t *v, int n, int coarse, int fine, int level, int r2, int rs)
{
    op(v, n, coarse, fine, level, 99, r2, 40, 50, 99, 0, 0, 0, rs);
}

static void lfo(uint8_t *v, int speed, int delay, int pmd, int pms)
{
    v[137] = (uint8_t)speed, v[138] = (uint8_t)delay, v[139] = (uint8_t)pmd, v[140] = 0;
    v[141] = 0, v[142] = 4, v[143] = (uint8_t)pms; /* a sine, free running */
}

static void name(uint8_t *v, const char *s)
{
    int i = 0;
    for (; i < 10 && s[i]; i++)
        v[145 + i] = (uint8_t)s[i];
    for (; i < 10; i++)
        v[145 + i] = ' ';
}

void rh_voice(int sound, uint8_t v[155])
{
    int i;
    for (i = 0; i < 155; i++)
        v[i] = 0;
    for (i = 126; i < 130; i++)
        v[i] = 99, v[i + 4] = 50; /* the pitch envelope stays at the key */
    v[134] = 4;                   /* algorithm 5: three stacks of two */
    v[136] = 1;                   /* every note starts the same */
    v[137] = 35, v[143] = 3, v[144] = 24;
    switch (sound) {
    case RH_SOUND_XYLO: /* a wooden bar: a short knock above a warm tone */
        strike(v, 1, 1, 0, 99, 48, 2);
        strike(v, 2, 3, 50, 70, 74, 2);
        strike(v, 3, 4, 0, 88, 64, 2);
        name(v, "BUNS XYLO ");
        break;
    case RH_SOUND_KALIMBA: /* a thumb piano: a round tone with a bright click of its tine */
        strike(v, 1, 1, 0, 99, 45, 2);
        op(v, 2, 1, 0, 52, 99, 72, 40, 50, 99, 20, 0, 0, 2);
        strike(v, 3, 5, 40, 70, 82, 2);
        name(v, "RAIN TINES");
        break;
    case RH_SOUND_FLUTE: /* breathy and sustained, with a chiff of breath at the start and a late vibrato */
        op(v, 1, 1, 0, 99, 62, 45, 35, 60, 99, 94, 90, 0, 0);
        op(v, 2, 1, 0, 56, 55, 40, 35, 60, 92, 85, 82, 0, 0);
        op(v, 3, 2, 0, 66, 62, 45, 35, 60, 99, 90, 85, 0, 0);
        op(v, 5, 6, 0, 58, 99, 80, 99, 60, 99, 0, 0, 0, 0);
        op(v, 6, 9, 0, 88, 99, 80, 99, 60, 99, 0, 0, 0, 0);
        v[135] = 7;
        lfo(v, 33, 55, 8, 3);
        name(v, "LAMB FLUTE");
        break;
    case RH_SOUND_MUSICBOX: /* a music box an octave up: a pure comb tooth with glassy partials */
        strike(v, 1, 1, 0, 99, 40, 2);
        strike(v, 2, 1, 0, 45, 66, 2);
        strike(v, 3, 3, 0, 82, 54, 2);
        strike(v, 4, 7, 0, 48, 78, 2);
        strike(v, 5, 6, 0, 66, 66, 2);
        v[144] = 36;
        name(v, "STAR BOX  ");
        break;
    case RH_SOUND_MARIMBA: /* a soft mallet on a rosewood bar */
        strike(v, 1, 1, 0, 99, 47, 2);
        strike(v, 2, 4, 0, 60, 82, 2);
        strike(v, 3, 4, 0, 80, 66, 2);
        name(v, "BOAT MARIM");
        break;
    case RH_SOUND_BELL: /* a tower bell: inharmonic partials that ring on */
        op(v, 1, 1, 0, 99, 99, 32, 22, 30, 99, 55, 0, 0, 1);
        op(v, 2, 3, 50, 66, 99, 38, 22, 30, 99, 40, 0, 0, 1);
        op(v, 3, 2, 38, 78, 99, 40, 22, 30, 99, 30, 0, 0, 1);
        op(v, 4, 1, 41, 58, 99, 45, 22, 30, 99, 30, 0, 0, 1);
        strike(v, 5, 5, 40, 58, 60, 1);
        name(v, "BELL TOWER");
        break;
    case RH_SOUND_TOYPIANO: /* a toy piano an octave up: metal rods struck by little hammers */
        op(v, 1, 1, 0, 99, 99, 46, 32, 55, 99, 30, 0, 0, 2);
        op(v, 2, 1, 0, 70, 99, 64, 38, 55, 99, 25, 0, 0, 2);
        strike(v, 3, 3, 7, 84, 56, 2);
        strike(v, 4, 9, 0, 52, 82, 2);
        v[144] = 36;
        name(v, "TOY PIANO ");
        break;
    case RH_SOUND_BANJO: /* a plucked banjo, twangy from feedback */
        strike(v, 1, 1, 0, 99, 47, 2);
        op(v, 2, 1, 0, 78, 99, 66, 42, 60, 99, 35, 0, 0, 2);
        strike(v, 3, 2, 0, 90, 56, 2);
        strike(v, 4, 3, 0, 70, 82, 2);
        strike(v, 5, 1, 0, 84, 62, 2);
        strike(v, 6, 1, 0, 78, 86, 2);
        v[135] = 6;
        name(v, "FARM BANJO");
        break;
    case RH_SOUND_HARP: /* a soft harp string */
        strike(v, 1, 1, 0, 99, 40, 2);
        strike(v, 2, 1, 0, 52, 62, 2);
        strike(v, 3, 2, 0, 68, 52, 2);
        strike(v, 4, 1, 0, 35, 70, 2);
        name(v, "SPIDERHARP");
        break;
    case RH_SOUND_SLEIGH: /* bells an octave up, with a jingle on top */
        strike(v, 1, 1, 0, 99, 42, 1);
        op(v, 2, 1, 0, 60, 99, 70, 40, 50, 99, 10, 0, 0, 1);
        strike(v, 3, 3, 50, 92, 50, 1);
        strike(v, 4, 7, 0, 66, 70, 1);
        strike(v, 5, 13, 0, 74, 76, 1);
        strike(v, 6, 17, 0, 84, 82, 1);
        v[135] = 7;
        v[144] = 36;
        name(v, "SLEIGHBELL");
        break;
    default: /* RH_SOUND_CALLIOPE: a party calliope, every operator a pipe */
        op(v, 1, 1, 0, 99, 80, 60, 50, 62, 99, 96, 94, 0, 0);
        op(v, 2, 2, 0, 90, 80, 60, 50, 62, 99, 96, 94, 0, 0);
        op(v, 3, 0, 0, 86, 80, 60, 50, 62, 99, 96, 94, 0, 0);
        op(v, 4, 3, 0, 76, 80, 60, 50, 62, 99, 96, 94, 0, 0);
        op(v, 5, 4, 0, 70, 80, 60, 50, 62, 99, 96, 94, 0, 0);
        op(v, 6, 8, 0, 34, 80, 60, 50, 62, 99, 96, 94, 0, 0);
        v[134] = 31; /* algorithm 32: six carriers */
        v[135] = 5;
        lfo(v, 40, 30, 10, 4);
        name(v, "BIRTHDAY  ");
        break;
    }
}

/* How each sound is heard: the reverb's room (0 room, 1 hall, 2 plate), its decay and its mix at Room 50, and a
 * little chorus for the sounds that shimmer. */
typedef struct {
    uint8_t room, decay, mix, chorus;
} rh_space_t;

static const rh_space_t SPACES[RH_SOUND_COUNT] = {
    {0, 35, 24, 0},  /* xylophone */
    {0, 40, 26, 0},  /* kalimba */
    {1, 45, 30, 0},  /* flute */
    {1, 55, 34, 25}, /* music box */
    {0, 40, 26, 0},  /* marimba */
    {1, 70, 38, 0},  /* bell */
    {0, 30, 22, 0},  /* toy piano */
    {0, 25, 16, 0},  /* banjo */
    {1, 50, 32, 0},  /* harp */
    {1, 55, 30, 30}, /* sleigh bells */
    {0, 35, 24, 40}, /* calliope */
};

int rh_fx(const int32_t row[RH_FIELDS], int cc[24], int value[24])
{
    int32_t r[RH_FIELDS];
    const rh_space_t *s;
    int i, n = 0, mix;
    for (i = 0; i < RH_FIELDS; i++)
        r[i] = row[i];
    rh_clamp(r);
    s = &SPACES[r[RH_SOUND]];
    mix = rh_clampi((s->mix * r[RH_ROOM] + 25) / 50, 0, 100);
#define SET(c, v) (cc[n] = (c), value[n] = (v), n++)
    SET(0, 0);                  /* the filter stays off */
    SET(4, mix > 0);
    SET(5, s->room);
    SET(6, s->decay);
    SET(7, mix);
    SET(8, 0);                  /* no echo */
    SET(12, 0);                 /* no drive */
    SET(16, s->chorus > 0);
    SET(17, 20);
    SET(18, 40);
    SET(19, s->chorus);
    SET(20, 0);                 /* no phaser */
#undef SET
    return n;
}
