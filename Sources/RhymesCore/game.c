/* Rhymes' game: which key comes next, what a key does, the lights, the stars, and the notes the game plays by
 * itself (the song when it is listened to; a fanfare, then the whole song with its chords, when it is finished). Time is in milliseconds on any clock
 * that wraps at 32 bits. Plain C with no C library, as the FM-1 firmware builds it too. */
#include "RhymesCore.h"
#include "internal.h"

#define LEAD_MS 600u           /* before a listened song's first note */
#define GAP_MS 200u            /* the next key stays dark this long after a right one, so a repeat shows */
#define BLINK_MS 640u          /* one blink: dark for GAP_MS, then lit */
#define WRONG_MS 1200u         /* after a wrong key, the right one stays lit this long */
#define HINT_MS 2500u          /* in Solo, how long a child waits before the light helps */
#define SHOW_MS 2600u          /* the keys' light show when a song is finished */
#define ENCORE_MS 3200u        /* the stars and fanfare, before the finished song plays again with its chords */
#define END_MS 300u            /* after a song plays itself, before it is ready to play again */
#define FANFARE_AT 450u        /* the fanfare starts this long after the last note */
#define FANFARE_STEP 130u
#define LONG_AGO 1000000u

static const int FANFARE[4] = {0, 4, 7, 12};

static uint32_t eighth_ms(const rh_game_t *g)
{
    int bpm = g->tune.tempo * g->speed;
    return bpm > 0 ? (uint32_t)(3000000 / bpm) : 300u;
}

/* When note i starts and stops while the song plays itself, from listen_at. */
static uint32_t note_on_at(const rh_game_t *g, int i)
{
    uint32_t e = 0;
    int k;
    for (k = 0; k < i && k < g->tune.count; k++)
        e += g->tune.len[k];
    return LEAD_MS + e * eighth_ms(g);
}

static uint32_t gate(uint32_t len)
{
    uint32_t g = len * 85u / 100u;
    return len > 90u && g < 90u ? 90u : g;
}

static uint32_t note_off_at(const rh_game_t *g, int i)
{
    return note_on_at(g, i) + gate(g->tune.len[i] * eighth_ms(g));
}

/* The chords sound at the start of each and again every half bar. Strike s: which chord, and when it starts and stops
 * from listen_at; 0 past the last. */
static int strike_at(const rh_game_t *g, int s, int *chord, uint32_t *on, uint32_t *off)
{
    uint32_t start = 0;
    int c, half = g->tune.half > 0 ? g->tune.half : 4;
    for (c = 0; c < g->tune.chords; c++) {
        int len = g->tune.chord_len[c], n = g->tune.chord_root[c] == 255 ? 0 : (len + half - 1) / half;
        if (s < n) {
            int left = len - s * half;
            *chord = c;
            *on = LEAD_MS + (start + (uint32_t)(s * half)) * eighth_ms(g);
            *off = *on + gate((uint32_t)(left < half ? left : half) * eighth_ms(g));
            return 1;
        }
        s -= n;
        start += (uint32_t)len;
    }
    return 0;
}

/* A chord's notes: the root as a bass between E2 and D#3, and its third and fifth (seventh, for a seventh chord)
 * between E3 and D#4, under the tune. */
static void chord_notes(const rh_game_t *g, int c, int out[3])
{
    int root = g->tune.chord_root[c], kind = g->tune.chord_kind[c];
    int third = root + (kind == RH_MINOR ? 3 : 4), top = root + (kind == RH_SEVENTH ? 10 : 7);
    out[0] = 40 + (root + 8) % 12;
    out[1] = 52 + (third + 8) % 12;
    out[2] = 52 + (top + 8) % 12;
}

static uint32_t fanfare_on(int i) { return FANFARE_AT + (uint32_t)i * FANFARE_STEP; }
static uint32_t fanfare_off(int i) { return fanfare_on(i) + (i == 3 ? 700u : 110u); }

void rh_game_settings(rh_game_t *g, const int32_t row[RH_FIELDS])
{
    int32_t r[RH_FIELDS];
    int i;
    for (i = 0; i < RH_FIELDS; i++)
        r[i] = row[i];
    rh_clamp(r);
    g->level = r[RH_LEVEL];
    g->lights = r[RH_LIGHTS];
    g->speed = r[RH_SPEED];
}

void rh_game_restart(rh_game_t *g, uint32_t now)
{
    g->state = RH_READY;
    g->pos = 0;
    g->misses = g->missed_here = g->streak = g->best_streak = g->stars = g->skipped = 0;
    g->last_right = g->last_wrong = g->done_at = g->listen_at = now - LONG_AGO;
    g->started = now;
    g->auto_next = g->strike_next = 0;
}

void rh_game_init(rh_game_t *g, const int32_t row[RH_FIELDS], uint32_t now)
{
    int32_t r[RH_FIELDS];
    int i;
    for (i = 0; i < RH_FIELDS; i++)
        r[i] = row[i];
    rh_clamp(r);
    rh_tune(r[RH_SONG], &g->tune);
    g->auto_count = 0;
    rh_game_settings(g, r);
    rh_game_restart(g, now);
}

int rh_game_stars(const rh_game_t *g)
{
    int n = g->tune.count;
    if (g->level == RH_MAGIC || g->skipped)
        return 1;
    if (g->misses * 10 <= n)
        return 3;
    if (g->misses * 4 <= n)
        return 2;
    return 1;
}

int rh_game_key(rh_game_t *g, int key, uint32_t now, int *note)
{
    int target;
    *note = rh_note_of(key);
    if (g->tune.count == 0)
        return RH_FREE;
    if (g->state == RH_DONE || g->state == RH_ENCORE)
        return RH_FREE; /* playing along with the celebration */
    if (g->state == RH_LISTEN)
        rh_game_restart(g, now);
    if (g->state == RH_READY) {
        g->state = RH_PLAYING;
        g->started = now;
    }
    target = g->tune.note[g->pos];
    if (g->level != RH_MAGIC && rh_key_of(target) != key) {
        if (!g->missed_here)
            g->misses++;
        g->missed_here = 1;
        g->streak = 0;
        g->last_wrong = now;
        *note = -1; /* a wrong key is silent: the lights show the way, not a sound */
        return RH_WRONG;
    }
    *note = target;
    g->pos++;
    g->streak++;
    if (g->streak > g->best_streak)
        g->best_streak = g->streak;
    g->missed_here = 0;
    g->last_right = now;
    if (g->pos >= g->tune.count) {
        g->state = RH_DONE;
        g->done_at = now;
        g->last_wrong = now - LONG_AGO;
        g->stars = rh_game_stars(g);
        g->auto_next = g->strike_next = 0;
    }
    return RH_RIGHT;
}

void rh_game_line(rh_game_t *g, int delta, uint32_t now)
{
    int line, pos = 0;
    if (g->tune.count == 0)
        return;
    line = g->state == RH_PLAYING || g->state == RH_READY ? g->tune.line[g->pos < g->tune.count ? g->pos : 0] : 0;
    line = rh_clampi(line + delta, 0, g->tune.lines - 1);
    rh_game_restart(g, now);
    while (pos < g->tune.count && g->tune.line[pos] < line)
        pos++;
    g->pos = pos;
    g->skipped = pos > 0;
}

void rh_game_listen(rh_game_t *g, uint32_t now)
{
    if (g->state == RH_LISTEN) {
        rh_game_restart(g, now);
        return;
    }
    rh_game_restart(g, now);
    if (g->tune.count == 0)
        return;
    g->state = RH_LISTEN;
    g->listen_at = now;
}

enum { PART_TUNE, PART_CHORD, PART_FANFARE };

static int started(const rh_game_t *g, int note)
{
    int i;
    for (i = 0; i < g->auto_count; i++)
        if (g->auto_note[i] == note)
            return 1;
    return 0;
}

/* A part's notes let go; a note another part still holds sounds on. */
static void let_go(rh_game_t *g, int part, int off[RH_AUTO_MAX], int *offs)
{
    int i = 0;
    while (i < g->auto_count) {
        int note = g->auto_note[i];
        if (g->auto_part[i] != part) {
            i++;
            continue;
        }
        g->auto_count--;
        g->auto_note[i] = g->auto_note[g->auto_count];
        g->auto_part[i] = g->auto_part[g->auto_count];
        if (!started(g, note) && *offs < RH_AUTO_MAX)
            off[(*offs)++] = note;
    }
}

static void start(rh_game_t *g, int note, int part, int on[RH_AUTO_MAX], int *ons)
{
    if (g->auto_count >= RH_AUTO_MAX)
        return;
    if (!started(g, note) && *ons < RH_AUTO_MAX)
        on[(*ons)++] = note;
    g->auto_note[g->auto_count] = note;
    g->auto_part[g->auto_count++] = part;
}

static int holds(const rh_game_t *g, int part)
{
    int i;
    for (i = 0; i < g->auto_count; i++)
        if (g->auto_part[i] == part)
            return 1;
    return 0;
}

/* The song playing itself, one note and one strike of its chords at most a step: the tune a note at a time, each
 * stopping at its own time or when the next begins, and in an encore the chords under it. */
static void play(rh_game_t *g, uint32_t now, int chords, int off[RH_AUTO_MAX], int *offs, int on[RH_AUTO_MAX],
                 int *ons)
{
    uint32_t t = now - g->listen_at, s_on = 0, s_off = 0, end;
    int c = 0, more = chords && strike_at(g, g->strike_next, &c, &s_on, &s_off);
    if (holds(g, PART_TUNE) && (t >= note_off_at(g, g->auto_next - 1) ||
                                (g->auto_next < g->tune.count && t >= note_on_at(g, g->auto_next))))
        let_go(g, PART_TUNE, off, offs);
    if (holds(g, PART_CHORD) && (t >= g->chord_off || (more && t >= s_on)))
        let_go(g, PART_CHORD, off, offs);
    if (g->auto_next < g->tune.count && t >= note_on_at(g, g->auto_next)) {
        start(g, g->tune.note[g->auto_next], PART_TUNE, on, ons);
        g->auto_next++;
    }
    if (more && t >= s_on) {
        int n[3], i;
        chord_notes(g, c, n);
        for (i = 0; i < 3; i++)
            start(g, n[i], PART_CHORD, on, ons);
        g->chord_off = s_off;
        g->strike_next++;
        more = strike_at(g, g->strike_next, &c, &s_on, &s_off);
    }
    end = note_off_at(g, g->tune.count - 1);
    if (chords && g->chord_off > end)
        end = g->chord_off;
    if (g->auto_next >= g->tune.count && !more && !g->auto_count && t >= end + END_MS)
        rh_game_restart(g, now);
}

void rh_game_step(rh_game_t *g, uint32_t now, int off[RH_AUTO_MAX], int *offs, int on[RH_AUTO_MAX], int *ons)
{
    *offs = *ons = 0;
    if (g->state == RH_LISTEN || g->state == RH_ENCORE) {
        play(g, now, g->state == RH_ENCORE, off, offs, on, ons);
        return;
    }
    if (g->state == RH_DONE) {
        uint32_t t = now - g->done_at;
        int base = rh_song_def(g->tune.song)->tonic;
        /* The fanfare, a note at a time, the last held. */
        if (holds(g, PART_FANFARE) && g->auto_next > 0 && t >= fanfare_off(g->auto_next - 1))
            let_go(g, PART_FANFARE, off, offs);
        if (g->auto_next < 4 && t >= fanfare_on(g->auto_next)) {
            let_go(g, PART_FANFARE, off, offs);
            if (t < fanfare_off(g->auto_next))
                start(g, base + FANFARE[g->auto_next], PART_FANFARE, on, ons);
            g->auto_next++;
        }
        if (t >= ENCORE_MS) {
            let_go(g, PART_FANFARE, off, offs);
            g->state = RH_ENCORE;
            g->listen_at = g->done_at + ENCORE_MS;
            g->auto_next = g->strike_next = 0;
            g->chord_off = 0;
        }
        return;
    }
    let_go(g, PART_TUNE, off, offs);
    let_go(g, PART_CHORD, off, offs);
    let_go(g, PART_FANFARE, off, offs);
}

uint32_t rh_game_due(const rh_game_t *g, uint32_t now)
{
    if (g->state == RH_LISTEN || g->state == RH_ENCORE || g->state == RH_DONE || g->auto_count)
        return now + 1u;
    return now + 1000u;
}

static int listen_index(const rh_game_t *g, uint32_t now)
{
    uint32_t t = now - g->listen_at;
    int i = g->tune.count - 1;
    while (i > 0 && note_on_at(g, i) > t)
        i--;
    return i;
}

void rh_key_window(const rh_tune_t *t, int *first, int *count)
{
    int lo = 15, hi = 0, i;
    for (i = 0; i < t->count; i++) {
        int k = rh_key_of(t->note[i]);
        if (k >= 0 && k < 16) {
            lo = k < lo ? k : lo;
            hi = k > hi ? k : hi;
        }
    }
    if (lo > hi)
        lo = 0, hi = 15;
    lo = lo > 0 ? lo - 1 : 0;
    hi = hi < 15 ? hi + 1 : 15;
    while (hi - lo + 1 < 8) {
        if (lo > 0)
            lo--;
        if (hi - lo + 1 < 8 && hi < 15)
            hi++;
    }
    *first = lo;
    *count = hi - lo + 1;
}

int rh_game_current(const rh_game_t *g, uint32_t now)
{
    if (g->state == RH_LISTEN || g->state == RH_ENCORE)
        return listen_index(g, now);
    if (g->state == RH_DONE || g->pos >= g->tune.count)
        return -1;
    return g->pos;
}

void rh_game_lights(const rh_game_t *g, uint32_t now, uint32_t *bright, uint32_t *dim)
{
    int key, next;
    uint32_t since, show;
    *bright = *dim = 0;
    if (g->tune.count == 0)
        return;
    if (g->state == RH_LISTEN || g->state == RH_ENCORE) {
        int i = listen_index(g, now);
        uint32_t t = now - g->listen_at;
        if (t >= note_on_at(g, i) && t < note_off_at(g, i))
            *bright = 1u << rh_key_of(g->tune.note[i]);
        return;
    }
    if (g->state == RH_DONE) {
        /* A light running up and down the keys, with the upper row twinkling. */
        uint32_t t = now - g->done_at, step = t / 55u, k = step % 30u;
        if (t >= SHOW_MS)
            return;
        k = k < 16u ? k : 30u - k;
        *bright = 1u << k;
        if (k > 0)
            *dim |= 1u << (k - 1);
        if (k < 15)
            *dim |= 1u << (k + 1);
        *dim |= 1u << (16 + (step * 7u) % 11u);
        *dim |= 1u << (16 + (step * 3u + 5u) % 11u);
        *dim &= ~*bright;
        return;
    }
    key = rh_key_of(g->tune.note[g->pos]);
    since = now - g->last_right;
    show = 1;
    if (g->level == RH_SOLO && g->pos > 0) {
        uint32_t waited = since < now - g->started ? since : now - g->started;
        show = waited >= HINT_MS || now - g->last_wrong < WRONG_MS;
    }
    if (show) {
        int lit;
        if (now - g->last_wrong < WRONG_MS)
            lit = 1;
        else if (since < GAP_MS)
            lit = 0;
        else if (g->lights == RH_STEADY)
            lit = 1;
        else
            lit = (since - GAP_MS) % BLINK_MS < BLINK_MS - GAP_MS;
        if (lit)
            *bright = 1u << key;
    }
    if (g->lights == RH_AHEAD && g->pos + 1 < g->tune.count && (show || g->level != RH_SOLO)) {
        next = rh_key_of(g->tune.note[g->pos + 1]);
        if (next != key)
            *dim = 1u << next;
    }
}

void rh_game_view(const rh_game_t *g, uint32_t now, int sounding, rh_view_t *v)
{
    int cur, first, i;
    v->count = g->tune.count;
    v->lines = g->tune.lines;
    v->done = g->state == RH_DONE ? (int)((now - g->done_at) / RH_SCENE_MS) : -1;
    if (v->done > 1000)
        v->done = 1000;
    v->listen = g->state == RH_LISTEN || g->state == RH_ENCORE;
    v->sounding = sounding;
    if (v->listen) {
        uint32_t t = now - g->listen_at;
        cur = listen_index(g, now);
        v->pos = t >= note_on_at(g, cur) ? cur + 1 : cur;
        v->hit = t >= note_on_at(g, cur) ? (int)((t - note_on_at(g, cur)) / RH_SCENE_MS) : 1000;
    } else {
        v->pos = g->pos;
        v->hit = now - g->last_right < 100000u ? (int)((now - g->last_right) / RH_SCENE_MS) : 1000;
    }
    v->miss = now - g->last_wrong < 100000u && g->state != RH_DONE ? (int)((now - g->last_wrong) / RH_SCENE_MS) : 1000;
    if (v->hit > 1000)
        v->hit = 1000;
    if (v->miss > 1000)
        v->miss = 1000;
    if (v->count == 0) {
        v->line = v->line_pos = v->line_len = 0;
        return;
    }
    /* The line of the next note, or the last line once the song is done. */
    cur = v->pos < v->count ? v->pos : v->count - 1;
    v->line = g->tune.line[cur];
    first = cur;
    while (first > 0 && g->tune.line[first - 1] == v->line)
        first--;
    v->line_len = 0;
    for (i = first; i < v->count && g->tune.line[i] == v->line; i++)
        v->line_len++;
    v->line_pos = v->pos - first;
    if (v->line_pos > v->line_len)
        v->line_pos = v->line_len;
}
