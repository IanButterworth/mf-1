/* Rhymes' songs: traditional nursery rhymes, their tunes and words. Every one is in the public domain. Each tune is
 * on the white keys from G3 to E5, in the middle of the FM-1's keys, so no song needs a change of octave.
 * Plain C with no C library, as the FM-1 firmware builds it too. */
#include "RhymesCore.h"
#include "internal.h"

/* A tune is written as notes, words and chords. A note is a letter, lower case from C4 and upper case from C5, a comma
 * after it for the octave below, then its length in eighths. "/" ends a line. The words have one syllable for each
 * note, split by spaces or hyphens, with "/" where the notes have it. A chord is its root, "m" for minor or "7" for a
 * seventh, a colon and its length in eighths, "-" for none; the chords last as long as the notes, and their "/" are
 * only for reading. A finished song plays itself again with its chords under it, a bass and two notes above. */
static const rh_song_def_t SONGS[] = {
    {"Hot Cross Buns", "Three notes: E, D and C", 0xE67E22u, 96, 60, RH_SOUND_XYLO, 4,
     "e2 d2 c4 e2 d2 c4 / c1 c1 c1 c1 d1 d1 d1 d1 / e2 d2 c4",
     "Hot cross buns! Hot cross buns! / One a pen-ny, two a pen-ny, / hot cross buns!",
     "C:2 G:2 C:4 C:2 G:2 C:4 / C:4 G:4 / C:2 G:2 C:4"},
    {"Rain, Rain, Go Away", "Three notes: G, E and A", 0x2E86DEu, 104, 60, RH_SOUND_KALIMBA, 4,
     "g2 e2 g1 g1 e2 g1 g1 e1 a1 g1 g1 e2 / g1 g1 e1 a1 g1 g1 e2 g2 e2 g1 g1 e4",
     "Rain, rain, go a-way, come a-gain an-oth-er day. / Lit-tle chil-dren want to play, rain, rain, go a-way!",
     "C:8 C:2 F:2 C:4 / C:2 F:2 C:4 G:4 C:6"},
    {"Mary's Little Lamb", "Walk the lamb to school", 0x27AE60u, 112, 60, RH_SOUND_FLUTE, 4,
     "e3 d1 c2 d2 e2 e2 e4 / d2 d2 d4 e2 g2 g4 / e3 d1 c2 d2 e2 e2 e2 e2 / d2 d2 e2 d2 c8",
     "Ma-ry had a lit-tle lamb, / lit-tle lamb, lit-tle lamb, / Ma-ry had a lit-tle lamb, its / "
     "fleece was white as snow.",
     "C:8 C:8 / G:8 C:8 / C:8 C:8 / G:8 C:8"},
    {"Twinkle, Twinkle", "Light up the night sky", 0xF1C40Fu, 100, 60, RH_SOUND_MUSICBOX, 4,
     "c2 c2 g2 g2 a2 a2 g4 / f2 f2 e2 e2 d2 d2 c4 / g2 g2 f2 f2 e2 e2 d4 / g2 g2 f2 f2 e2 e2 d4 / "
     "c2 c2 g2 g2 a2 a2 g4 / f2 f2 e2 e2 d2 d2 c4",
     "Twin-kle, twin-kle, lit-tle star, / how I won-der what you are! / Up a-bove the world so high, / "
     "like a dia-mond in the sky. / Twin-kle, twin-kle, lit-tle star, / how I won-der what you are!",
     "C:4 C:4 F:4 C:4 / F:4 C:4 G:4 C:4 / C:4 F:4 C:4 G:4 / C:4 F:4 C:4 G:4 / C:4 C:4 F:4 C:4 / F:4 C:4 G:4 C:4"},
    {"Row Your Boat", "Row across the river", 0x16A085u, 96, 60, RH_SOUND_MARIMBA, 3,
     "c3 c3 c2 d1 e3 / e2 d1 e2 f1 g6 / C1 C1 C1 g1 g1 g1 e1 e1 e1 c1 c1 c1 / g2 f1 e2 d1 c6",
     "Row, row, row your boat, / gent-ly down the stream. / Mer-ri-ly, mer-ri-ly, mer-ri-ly, mer-ri-ly, / "
     "life is but a dream.",
     "C:6 C:6 / C:6 C:6 / C:12 / G:6 C:6"},
    {"Are You Sleeping?", "Wake up Brother John", 0x8E44ADu, 104, 60, RH_SOUND_BELL, 4,
     "c2 d2 e2 c2 c2 d2 e2 c2 / e2 f2 g4 e2 f2 g4 / g1 a1 g1 f1 e2 c2 g1 a1 g1 f1 e2 c2 / c2 g,2 c4 c2 g,2 c4",
     "Are you sleep-ing, are you sleep-ing, / Bro-ther John, Bro-ther John? / "
     "Morn-ing bells are ring-ing, morn-ing bells are ring-ing, / Ding, dang, dong! Ding, dang, dong!",
     "C:8 C:8 / C:8 C:8 / C:8 C:8 / C:2 G:2 C:4 C:2 G:2 C:4"},
    {"London Bridge", "Build it stone by stone", 0x5D6D7Eu, 112, 60, RH_SOUND_TOYPIANO, 4,
     "g3 a1 g2 f2 e2 f2 g4 / d2 e2 f4 e2 f2 g4 / g3 a1 g2 f2 e2 f2 g4 / d4 g4 e2 c6",
     "Lon-don Bridge is fall-ing down, / fall-ing down, fall-ing down. / Lon-don Bridge is fall-ing down, / "
     "my fair la-dy!",
     "C:8 C:8 / G:8 C:8 / C:8 C:8 / G:8 C:8"},
    {"Old MacDonald", "Down on the farm", 0xA0522Du, 120, 67, RH_SOUND_BANJO, 4,
     "g2 g2 g2 d2 e2 e2 d4 / b2 b2 a2 a2 g6 / d2 g2 g2 g2 d2 e2 e2 d4 / b2 b2 a2 a2 g6 / "
     "d1 d1 g2 g2 g4 d1 d1 g2 g2 g4 / g1 g1 g2 g1 g1 g2 g1 g1 g1 g1 g2 g2 / g2 g2 g2 d2 e2 e2 d4 / b2 b2 a2 a2 g6",
     "Old Mac-Don-ald had a farm, / E-I-E-I-O! / And on that farm he had a cow, / E-I-E-I-O! / "
     "With a moo moo here and a moo moo there, / here a moo, there a moo, ev-ery-where a moo moo, / "
     "Old Mac-Don-ald had a farm, / E-I-E-I-O!",
     "G:8 C:4 G:4 / G:4 D:4 G:6 / G:10 C:4 G:4 / G:4 D:4 G:6 / G:10 G:10 / G:8 G:8 / G:8 C:4 G:4 / G:4 D:4 G:6"},
    {"Itsy Bitsy Spider", "Up the water spout", 0x6C5CE7u, 104, 60, RH_SOUND_HARP, 3,
     "g,1 c2 c1 c2 d1 e3 e2 e1 d2 c1 d2 e1 c4 / e3 e2 f1 g3 g3 f2 e1 f2 g1 e6 / c3 c2 d1 e3 e3 d2 c1 d2 e1 c6 / "
     "g,1 g,1 c2 c1 c2 d1 e3 e2 e1 d2 c1 d2 e1 c4",
     "The it-sy bit-sy spi-der climbed up the wa-ter spout. / Down came the rain and washed the spi-der out. / "
     "Out came the sun and dried up all the rain, / And the it-sy bit-sy spi-der climbed up the spout a-gain.",
     "-:1 C:6 C:6 G:6 C:4 / C:6 C:6 G7:6 C:6 / C:6 C:6 G7:6 C:6 / -:2 C:6 C:6 G7:6 C:4"},
    {"Jingle Bells", "A sleigh ride in the snow", 0xC0392Bu, 126, 60, RH_SOUND_SLEIGH, 4,
     "e2 e2 e4 e2 e2 e4 e2 g2 c3 d1 e8 / f2 f2 f3 f1 f2 e2 e2 e1 e1 e2 d2 d2 e2 d4 g4 / "
     "e2 e2 e4 e2 e2 e4 e2 g2 c3 d1 e8 / f2 f2 f3 f1 f2 e2 e2 e1 e1 g2 g2 f2 d2 c8",
     "Jin-gle bells, jin-gle bells, jin-gle all the way! / "
     "Oh what fun it is to ride in a one-horse o-pen sleigh, hey! / "
     "Jin-gle bells, jin-gle bells, jin-gle all the way! / "
     "Oh what fun it is to ride in a one-horse o-pen sleigh!",
     "C:8 C:8 C:8 C:8 / F:8 C:8 G:8 G7:8 / C:8 C:8 C:8 C:8 / F:8 C:8 G7:8 C:8"},
    {"Happy Birthday", "Light the candles", 0xD6336Cu, 96, 60, RH_SOUND_CALLIOPE, 3,
     "g,1 g,1 a,2 g,2 c2 b,4 / g,1 g,1 a,2 g,2 d2 c4 / g,1 g,1 g2 e2 c2 b,2 a,4 / f1 f1 e2 c2 d2 c4",
     "Hap-py birth-day to you, / hap-py birth-day to you, / hap-py birth-day, dear Ted-dy, / "
     "hap-py birth-day to you!",
     "-:2 C:6 G:4 / G:2 G:6 C:4 / C:2 C:6 F:6 / F:2 C:4 G:2 C:4"},
};

#define NSONGS ((int)(sizeof SONGS / sizeof SONGS[0]))

static const char *const LEVELS[RH_LEVEL_COUNT] = {"Magic", "Guide", "Solo"};
static const char *const SOUNDS[RH_SOUND_COUNT] = {"Xylophone", "Kalimba", "Flute", "Music box", "Marimba", "Bells",
                                                    "Toy piano", "Banjo", "Harp", "Sleigh bells", "Calliope"};
static const char *const LIGHTS[RH_LIGHTS_COUNT] = {"Blink", "Steady", "Ahead"};
/* The colours children's bells and tubes use, C to B. */
static const uint32_t NOTE_COLOURS[7] = {0xE53935u, 0xFB8C00u, 0xFDD835u, 0x43A047u, 0x00ACC1u, 0x5E35B1u, 0xD81B60u};
/* Which of C..B each pitch class is shown as; the sharps take the colour of the note below. */
static const uint8_t PITCH_NAME[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};

/* The FM-1's keys, in semitones from the lowest: the lower row and the upper. */
static const uint8_t LOWER[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t UPPER[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

int rh_song_count(void) { return NSONGS; }

const rh_song_def_t *rh_song_def(int song) { return &SONGS[rh_clampi(song, 0, NSONGS - 1)]; }

const char *rh_song_title(int song) { return rh_song_def(song)->title; }
const char *rh_song_blurb(int song) { return rh_song_def(song)->blurb; }
uint32_t rh_song_accent(int song) { return rh_song_def(song)->accent; }
int rh_song_sound(int song) { return rh_song_def(song)->sound; }
const char *rh_level_name(int level) { return LEVELS[rh_clampi(level, 0, RH_LEVEL_COUNT - 1)]; }
const char *rh_lights_name(int lights) { return LIGHTS[rh_clampi(lights, 0, RH_LIGHTS_COUNT - 1)]; }

int rh_sound_count(void) { return RH_SOUND_COUNT; }
const char *rh_sound_name(int sound) { return SOUNDS[rh_clampi(sound, 0, RH_SOUND_COUNT - 1)]; }

uint32_t rh_note_colour(int note)
{
    return NOTE_COLOURS[PITCH_NAME[((note % 12) + 12) % 12]];
}

int rh_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void rh_default(int song, int32_t out[RH_FIELDS])
{
    out[RH_SONG] = rh_clampi(song, 0, NSONGS - 1);
    out[RH_LEVEL] = RH_GUIDE;
    out[RH_SPEED] = 100;
    out[RH_ROOM] = 50;
    out[RH_LIGHTS] = RH_BLINK;
    out[RH_SOUND] = rh_song_def(out[RH_SONG])->sound;
}

void rh_range(int field, int *lo, int *hi)
{
    int a = 0, b = 100;
    switch (field) {
    case RH_SONG: b = NSONGS - 1; break;
    case RH_LEVEL: b = RH_LEVEL_COUNT - 1; break;
    case RH_SPEED: a = 50, b = 150; break;
    case RH_LIGHTS: b = RH_LIGHTS_COUNT - 1; break;
    case RH_SOUND: b = RH_SOUND_COUNT - 1; break;
    }
    if (lo)
        *lo = a;
    if (hi)
        *hi = b;
}

void rh_clamp(int32_t row[RH_FIELDS])
{
    int i, lo, hi;
    for (i = 0; i < RH_FIELDS; i++) {
        rh_range(i, &lo, &hi);
        row[i] = rh_clampi(row[i], lo, hi);
    }
}

int rh_key_of(int note)
{
    int s = note - RH_KEY_BASE, k;
    for (k = 0; k < 16; k++)
        if (LOWER[k] == s)
            return k;
    for (k = 0; k < 11; k++)
        if (UPPER[k] == s)
            return 16 + k;
    return -1;
}

int rh_note_of(int key)
{
    key = rh_clampi(key, 0, RH_KEYS - 1);
    return RH_KEY_BASE + (key < 16 ? LOWER[key] : UPPER[key - 16]);
}

static int is_space(char c) { return c == ' ' || c == '\n' || c == '\t'; }

/* The next note of a tune at *p: its pitch and length, or a line break as pitch -1. Returns 0 at the end, -2 on
 * a mistake. */
static int next_note(const char **p, int *pitch, int *len)
{
    static const int SEMIS[7] = {9, 11, 0, 2, 4, 5, 7}; /* a..g */
    const char *s = *p;
    int n = 0, base;
    while (is_space(*s))
        s++;
    if (!*s) {
        *p = s;
        return 0;
    }
    if (*s == '/') {
        *p = s + 1;
        *pitch = -1;
        return 1;
    }
    if (*s >= 'a' && *s <= 'g')
        base = 60 + SEMIS[*s - 'a'];
    else if (*s >= 'A' && *s <= 'G')
        base = 72 + SEMIS[*s - 'A'];
    else
        return -2;
    s++;
    while (*s == ',') {
        base -= 12;
        s++;
    }
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (*s++ - '0');
    if (n <= 0 || n > 255)
        return -2;
    *p = s;
    *pitch = base;
    *len = n;
    return 1;
}

/* The next syllable of the words at *p: where it starts and how long it is, or a line break as width 0. A hyphen
 * stays with the syllable before it. */
static int next_syllable(const char *words, const char **p, int *at, int *width)
{
    const char *s = *p, *start;
    while (is_space(*s) || *s == '-')
        s++;
    if (!*s) {
        *p = s;
        return 0;
    }
    if (*s == '/') {
        *p = s + 1;
        *at = (int)(s - words);
        *width = 0;
        return 1;
    }
    start = s;
    while (*s && !is_space(*s) && *s != '-' && *s != '/')
        s++;
    if (*s == '-')
        s++;
    *at = (int)(start - words);
    *width = (int)(s - start);
    *p = s;
    return 1;
}

/* The chords at *p, into the tune; returns their length in eighths, -1 on a mistake. */
static int read_chords(const char *p, rh_tune_t *t)
{
    static const int8_t ROOTS[7] = {9, 11, 0, 2, 4, 5, 7}; /* A..G */
    int total = 0;
    t->chords = 0;
    for (;;) {
        int root = 255, kind = RH_MAJOR, n = 0;
        while (is_space(*p) || *p == '/')
            p++;
        if (!*p)
            return total;
        if (*p >= 'A' && *p <= 'G')
            root = ROOTS[*p - 'A'];
        else if (*p != '-')
            return -1;
        p++;
        if (*p == 'm')
            kind = RH_MINOR, p++;
        else if (*p == '7')
            kind = RH_SEVENTH, p++;
        if (*p++ != ':')
            return -1;
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        if (n <= 0 || n > 255 || t->chords >= RH_MAX_CHORDS)
            return -1;
        t->chord_root[t->chords] = (uint8_t)root;
        t->chord_kind[t->chords] = (uint8_t)kind;
        t->chord_len[t->chords] = (uint8_t)n;
        t->chords++;
        total += n;
    }
}

int rh_tune(int song, rh_tune_t *t)
{
    const rh_song_def_t *d;
    const char *np, *wp;
    int line = 0, n = 0, pitch = 0, len = 0, at = 0, width = 0, r, w;
    if (song < 0 || song >= NSONGS || !t)
        return 0;
    d = &SONGS[song];
    np = d->notes;
    wp = d->words;
    t->song = song;
    t->tempo = d->tempo;
    t->half = d->half;
    t->count = t->lines = 0;
    for (;;) {
        r = next_note(&np, &pitch, &len);
        w = next_syllable(d->words, &wp, &at, &width);
        if (r < 0 || r != w)
            return 0;
        if (r == 0)
            break;
        if ((pitch < 0) != (width == 0))
            return 0;
        if (pitch < 0) {
            line++;
            continue;
        }
        if (n >= RH_MAX_NOTES || rh_key_of(pitch) < 0)
            return 0;
        t->note[n] = (uint8_t)pitch;
        t->len[n] = (uint8_t)len;
        t->line[n] = (uint8_t)line;
        t->at[n] = (uint16_t)at;
        t->width[n] = (uint8_t)width;
        n++;
    }
    {
        int k, eighths = 0;
        for (k = 0; k < n; k++)
            eighths += t->len[k];
        if (read_chords(d->chords, t) != eighths)
            return 0;
    }
    t->count = n;
    t->lines = n ? line + 1 : 0;
    return n;
}

void rh_syllable(const rh_tune_t *t, int i, char out[16])
{
    const char *w;
    int k, n;
    out[0] = 0;
    if (!t || i < 0 || i >= t->count)
        return;
    w = SONGS[t->song].words + t->at[i];
    n = t->width[i] < 15 ? t->width[i] : 15;
    for (k = 0; k < n; k++)
        out[k] = w[k];
    out[n] = 0;
}
