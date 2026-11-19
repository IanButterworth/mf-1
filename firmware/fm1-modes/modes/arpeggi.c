/* Arpeggi as a mode: two arpeggiators playing one pool of notes. Its scenes and sounds are firmware/arpeggi/dsp's
 * port of the desktop's ArpeggiCore, its pool and lanes are ArpeggiCore's own arp.c, and its player runs them in the
 * audio interrupt (all checked by firmware/arpeggi/tests/run_parity.sh). This file is the panel, the keys that feed
 * the pool and the night sky on the screen. Included by app.c. */
#include "ap.h"
#include "ap_player.h"

enum { AG_SHAPE = 1, AG_RATE, AG_RHYTHM, AG_PERCENT, AG_HOLD, AG_OCT, AG_SIGNED, AG_SOUND, AG_RELATION, AG_STEPS };

#define AG_NIGHT 0x0E1022u
#define AG_INK 0xECE9FFu
#define AG_AMBER 0xFFB547u
#define AG_BLUE 0x4FD8FFu
#define AG_ROSE 0xFF5DA2u
#define AG_FADED 0x7A7DA8u
#define AG_PALE 0x2C2F56u
#define AG_FRAME_MS 20u                  /* while the lanes play, frames back to back */
#define AG_SKY_X 9
#define AG_SKY_Y 54
#define AG_SKY_W 222
#define AG_SKY_H 92
#define AG_WINDOW 110250u                /* how far back the sky reaches: 2.5 s of samples */
#define AG_HITS 32

/* Whether the keys feed the pool (saved with the scenes). */
static uint8_t ag_state[1] = {1};
#define ag_writes ag_state[0]

static uint8_t ag_playing;
static int8_t ag_key_pool[NKEYS];        /* the pool note each held key fed, or -1 */
static ApProc ag_sent;                   /* what the player was last given */
static int ag_sent_hold = -1, ag_sent_ring = -1;
static int ag_relation = -1;             /* the relation last seen, so a turn of RELATION brings B in */
/* what the screen draws, taken from the player once a frame */
static ApPool ag_pool;
static ap_hit_t ag_hits[AG_HITS];
static int ag_nhits;
static int64_t ag_tick;

/* The player's way to the engine: the core's note counts, which the keys share. */
int ap_refs(int note) { return md_refs(note); }
void ap_hold(int note, int strike) { md_hold(note, strike); }
void ap_let_go(int note) { md_let_go(note); }

/* ------------------------------------------------------------------ the player */

/* The lanes the scene and the octave make: the pool keeps notes without the octave, so OCT- and OCT+ move what is
 * playing. */
static void ag_proc(ApProc *p)
{
    ap_proc_make(row, p);
    p->transpose = octave * 12;
}

/* Hands the player the settings, the interrupt held off; a change keeps the time. */
static void ag_send(void)
{
    uint32_t f;
    ag_proc(&ag_sent);
    ag_sent_hold = value(AP_HOLD_MODE), ag_sent_ring = value(AP_RING_SIZE);
    f = plat_lock();
    ap_player_set(&ag_sent);
    ap_player_mode(ag_sent_hold, ag_sent_ring);
    plat_unlock(f);
}

static int ag_same(const ApProc *a, const ApProc *b)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    unsigned i;
    for (i = 0; i < sizeof *a; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

/* With an empty pool, PLAY starts the scene with its own chord. */
static void ag_play(void)
{
    int8_t seed[AP_MAX_POOL];
    int n = ap_default_seed(item(), seed);
    int64_t t;
    uint32_t f;
    ag_send();
    f = plat_lock();
    if (!ap_player_start())
        ap_player_seed(seed, n);
    ag_playing = (uint8_t)ap_player_now(&t);
    plat_unlock(f);
    ui.redraw = 1;
}

static void ag_stop(void)
{
    uint32_t f = plat_lock();
    ap_player_stop();
    plat_unlock(f);
    ag_playing = 0;
    ui.redraw = 1;
}

static void ag_clear(void)
{
    uint32_t f = plat_lock();
    ap_player_clear();
    plat_unlock(f);
    ag_playing = 0;
    show_pill("Notes cleared", 0);
}

/* ------------------------------------------------------------------ the keys */

/* While the keys feed the arps, a key's note goes to the pool and sounds only as the lanes play it; else it plays
 * along at once. */
static int ag_key_notes(const int32_t *s, int note, int left, int out[8])
{
    int k = note - 53 - 12 * octave, n = note - 12 * octave;
    int64_t t;
    uint32_t f;
    (void)s, (void)left;
    if (!ag_writes || k < 0 || k >= NKEYS) {
        out[0] = note;
        return 1;
    }
    ag_key_pool[k] = (int8_t)n;
    f = plat_lock();
    ap_player_key_down(n);
    ag_playing = (uint8_t)ap_player_now(&t);
    plat_unlock(f);
    ui.redraw = 1;
    return 0;
}

static void ag_key_up(int k)
{
    int64_t t;
    uint32_t f;
    if (k < 0 || k >= NKEYS || ag_key_pool[k] < 0)
        return;
    f = plat_lock();
    ap_player_key_up(ag_key_pool[k]);
    ag_playing = (uint8_t)ap_player_now(&t);
    plat_unlock(f);
    ag_key_pool[k] = -1;
}

/* ------------------------------------------------------------------ the mode's side of the core */

/* The rows are saved a byte a field, so a negative pitch comes back as a byte over 127. */
static void ag_clamp(int32_t *s)
{
    if (s[AP_B_PITCH] > 127)
        s[AP_B_PITCH] -= 256;
    ap_clamp(s);
}

static void ag_pattern(const int32_t *s, md_pattern_t *p)
{
    p->on = 0, p->mode = 0, p->octaves = 1, p->rate = 0, p->gate = 50, p->tempo = s[AP_TEMPO];
}

static int ag_mono(const int32_t *s) { return (void)s, 0; }
static int ag_releases(int field) { return (void)field, 0; }
static const char *ag_item_name(int i) { return ap_scene(i)->title; }

static void ag_format(int fmt, int v, char *out)
{
    char n[12];
    switch (fmt) {
    case AG_SHAPE: text_cat(out, ap_shape_name(v)); break;
    case AG_RATE: text_cat(out, ap_rate_name(v)); break;
    case AG_RHYTHM: text_cat(out, ap_rhythm_name(v)); break;
    case AG_HOLD: text_cat(out, ap_hold_name(v)); break;
    case AG_SOUND: text_cat(out, ap_sound_name(v)); break;
    case AG_RELATION: text_cat(out, ap_relation_name(v)); break;
    case AG_PERCENT:
        int_text(v, n);
        text_cat(out, n);
        text_cat(out, "%");
        break;
    case AG_OCT:
        int_text(v, n);
        text_cat(out, n);
        text_cat(out, " oct");
        break;
    case AG_SIGNED:
        int_text(v, n);
        text_cat(out, v > 0 ? "+" : "");
        text_cat(out, n);
        break;
    case AG_STEPS:
        int_text(v, n);
        text_cat(out, n);
        text_cat(out, "/16");
        break;
    }
}

/* Whether a value does anything in the scene as set, as on the desktop: the values a relation ignores are greyed. */
static int ag_applies(int field)
{
    int duo = value(AP_DUO) != 0, rel = value(AP_RELATION);
    switch (field) {
    case AP_B_SHAPE: return duo && rel != AP_CANON && rel != AP_MIRROR;
    case AP_B_RATE:
    case AP_B_RHYTHM: return duo && rel != AP_HOCKET && rel != AP_CANON;
    case AP_B_GATE:
    case AP_B_PITCH: return duo;
    case AP_RING_SIZE: return value(AP_HOLD_MODE) == AP_RING;
    case AP_ECHO_STEPS: return value(AP_ECHO) > 0;
    default: return 1;
    }
}

static void ag_press(uint32_t b)
{
    switch (b) {
    case B_EDIT: cycle(AP_HOLD_MODE, "Hold", AG_HOLD); break;
    case B_GLO:
        set_field(AP_DUO, value(AP_DUO) ? 0 : 1);
        show_pill(value(AP_DUO) ? "A and B" : "A alone", 0);
        break;
    case B_PLAY:
        if (ag_playing) {
            ag_stop();
            show_pill("Stopped", 0);
        } else {
            ag_play();
            show_pill(ap_scene(item())->title, 0);
        }
        break;
    case B_REC:
        ag_writes = !ag_writes;
        mark_dirty();
        show_pill(ag_writes ? "Keys feed the arps" : "Keys play along", 0);
        break;
    }
}

/* As the desktop's knobs: a step of two only on the long ranges. */
static int ag_knob(int k, int delta)
{
    const md_param_t *p = &M->params[page][k];
    char t[16];
    int lo, hi;
    ap_range(p->field, &lo, &hi);
    set_field(p->field, value(p->field) + delta * (hi - lo + 1 > 60 ? 2 : 1));
    format(p->fmt, value(p->field), t);
    show_pill(p->name, t);
    return 1;
}

/* PLAY is lit on each beat and dim between; REC while the keys feed the arps; GLO while B plays. */
static int ag_beat(void) { return ag_playing && (uint32_t)ag_tick % AP_PPQ < AP_PPQ / 5; }

static uint32_t ag_lit(void)
{
    uint32_t b = ag_beat() ? 1u << B_PLAY : 0u;
    if (ag_writes)
        b |= 1u << B_REC;
    if (value(AP_DUO))
        b |= 1u << B_GLO;
    return b;
}

static uint32_t ag_dim(void) { return ag_playing && !ag_beat() ? 1u << B_PLAY : 0u; }
static int ag_busy(void) { return ag_playing; }

/* Every start is a stopped one, with an empty pool. */
static void ag_start(void)
{
    uint32_t f;
    int i;
    ag_playing = 0;
    ag_nhits = 0;
    for (i = 0; i < NKEYS; i++)
        ag_key_pool[i] = -1;
    f = plat_lock();
    ap_player_reset();
    plat_unlock(f);
    ag_relation = value(AP_RELATION);
    ag_send();
}

/* A new scene keeps the pool, and the lanes go on in the new scene's way. */
static void ag_selected(void)
{
    ag_relation = value(AP_RELATION);
    ag_send();
}

static void ag_frame(uint32_t now)
{
    ApProc p;
    uint32_t f;
    int was = ag_playing;
    /* A turn of RELATION brings B in, as on the desktop. */
    if (value(AP_RELATION) != ag_relation) {
        ag_relation = value(AP_RELATION);
        if (!value(AP_DUO))
            set_field(AP_DUO, 1);
    }
    ag_proc(&p);
    if (!ag_same(&p, &ag_sent) || value(AP_HOLD_MODE) != ag_sent_hold || value(AP_RING_SIZE) != ag_sent_ring)
        ag_send();
    f = plat_lock();
    ag_playing = (uint8_t)ap_player_now(&ag_tick);
    ap_player_pool(&ag_pool);
    ag_nhits = ap_player_recent(ag_hits, AG_HITS);
    plat_unlock(f);
    if (was != ag_playing)
        ui.redraw = 1;
    if ((ag_playing || (ag_nhits && ag_hits[0].ago < AG_WINDOW + ag_hits[0].length)) && now - ui.drawn_at >= AG_FRAME_MS)
        ui.redraw = 1;
}

/* ------------------------------------------------------------------ the screen */

/* A colour faded toward the night by k/256. */
static uint32_t ag_mix(uint32_t c, int k)
{
    uint32_t r = ((c >> 16 & 255u) * (uint32_t)k + (AG_NIGHT >> 16 & 255u) * (uint32_t)(256 - k)) >> 8;
    uint32_t g = ((c >> 8 & 255u) * (uint32_t)k + (AG_NIGHT >> 8 & 255u) * (uint32_t)(256 - k)) >> 8;
    uint32_t b = ((c & 255u) * (uint32_t)k + (AG_NIGHT & 255u) * (uint32_t)(256 - k)) >> 8;
    return r << 16 | g << 8 | b;
}

/* Text with its letters spaced out, as the desktop's titles are. */
static int32_t ag_spaced_w(const felucca_font_t *f, const char *s, int32_t gap)
{
    char c[2] = {0, 0};
    int32_t w = 0;
    for (; *s; s++) {
        c[0] = *s;
        w += text_w(f, c) + (s[1] ? gap : 0);
    }
    return w;
}

static void ag_spaced(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint32_t rgb, int32_t gap, int align)
{
    char c[2] = {0, 0};
    if (align > 0)
        x -= ag_spaced_w(f, s, gap);
    for (; *s; s++) {
        c[0] = *s;
        text_at(x, y, f, c, rgb, 0);
        x += text_w(f, c) + gap;
    }
}

static void ag_note_name(int n, char *out)
{
    static const char *const NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[12];
    out[0] = 0;
    text_cat(out, NAMES[(n % 12 + 12) % 12]);
    int_text(n / 12 - 1, o);
    text_cat(out, o);
}

/* A few fixed stars, the same every frame. */
static void ag_draw_stars(void)
{
    uint32_t x = 0x9E3779B9u;
    int i;
    for (i = 0; i < 34; i++) {
        int32_t px, py;
        x = x * 1664525u + 1013904223u;
        px = (int32_t)(x % 232u) + 4;
        x = x * 1664525u + 1013904223u;
        py = (int32_t)(x % 110u) + 36;
        fill(px, py, x % 7u == 0u ? 2 : 1, x % 7u == 0u ? 2 : 1, AG_PALE);
    }
}

/* The pool slot by slot: in a ring, Ring slots with the one the next key fills marked; else the notes as they came. */
static void ag_draw_strip(void)
{
    int ring = ag_pool.hold == AP_RING, slots = ring ? ag_pool.ring : ag_pool.count, i, full;
    int32_t w, x0;
    if (!ag_pool.count && !ring) {
        text_at(120, 37, &FONT_T, ag_writes ? "PLAY A CHORD, OR PRESS PLAY" : "KEYS PLAY ALONG", AG_FADED, 1);
        return;
    }
    if (slots < 1)
        slots = 1;
    w = (222 - (slots - 1) * 3) / slots;
    w = w > 36 ? 36 : w;
    x0 = 120 - (slots * w + (slots - 1) * 3) / 2;
    full = ring && ag_pool.count == slots;
    for (i = 0; i < slots; i++) {
        int32_t x = x0 + i * (w + 3);
        if (i < ag_pool.count) {
            char t[8];
            fill(x, 33, w, 17, AG_PALE);
            if ((full && i == ag_pool.next) || (!full && ring && i == ag_pool.count))
                frame_rect(x, 33, w, 17, AG_ROSE);
            ag_note_name(ag_pool.note[i] + octave * 12, t);
            text_at(x + w / 2, 36, w < 22 ? &FONT_T : &FONT_XS, t, AG_INK, 1);
        } else {
            frame_rect(x, 33, w, 17, ring && !full && i == ag_pool.count ? AG_ROSE : AG_PALE);
        }
    }
}

/* The sky: each pool note a faint string in every octave of the span, the beats ticking along the bottom, and each
 * note the lanes struck a streak at its pitch, as long as it was held, drifting left as it ages. */
static void ag_draw_sky(void)
{
    int span = value(AP_SPAN), pitch = value(AP_DUO) ? value(AP_B_PITCH) : 0, shift = octave * 12;
    int lo = 127, hi = 0, i, o;
    for (i = 0; i < ag_pool.count; i++) {
        lo = ag_pool.note[i] < lo ? ag_pool.note[i] : lo;
        hi = ag_pool.note[i] > hi ? ag_pool.note[i] : hi;
    }
    if (lo > hi)
        lo = 60, hi = 72;
    lo += shift + (pitch < 0 ? pitch : 0);
    hi += shift + 12 * (span - 1) + (pitch > 0 ? pitch : 0);
    for (i = 0; i < ag_nhits; i++)
        if (ag_hits[i].ago < AG_WINDOW) {
            lo = ag_hits[i].note < lo ? ag_hits[i].note : lo;
            hi = ag_hits[i].note > hi ? ag_hits[i].note : hi;
        }
    lo -= 2, hi += 2;
    if (hi - lo < 14)
        hi = lo + 14;
#define AG_Y(n) (AG_SKY_Y + AG_SKY_H - ((n) - lo) * AG_SKY_H / (hi - lo))
#define AG_X(ago) (AG_SKY_X + AG_SKY_W - (int32_t)((uint32_t)(ago) / 10u * (uint32_t)AG_SKY_W / (AG_WINDOW / 10u)))
    for (o = 0; o < span; o++)
        for (i = 0; i < ag_pool.count; i++)
            fill(AG_SKY_X, AG_Y(ag_pool.note[i] + shift + 12 * o), AG_SKY_W, 1, AG_PALE);
    if (ag_playing) {
        /* a tick in samples is 1575 / (2 * tempo); the ticks fit 32 bits for 44 hours */
        uint32_t now = (uint32_t)ag_tick;
        int32_t b = (int32_t)(now / AP_PPQ);
        for (; b >= 0; b--) {
            uint32_t ago = (now - (uint32_t)b * AP_PPQ) * 1575u / (2u * (uint32_t)value(AP_TEMPO));
            int bar = b % 4 == 0;
            if (ago > AG_WINDOW)
                break;
            fill(AG_X(ago), AG_SKY_Y + AG_SKY_H + 2, 1, bar ? 4 : 2, bar ? AG_FADED : AG_PALE);
        }
    }
    fill(AG_SKY_X + AG_SKY_W, AG_SKY_Y, 1, AG_SKY_H, AG_PALE);
    /* oldest first, so the newest lie on top */
    for (i = ag_nhits - 1; i >= 0; i--) {
        const ap_hit_t *h = &ag_hits[i];
        uint32_t len = h->length > 1323u ? h->length : 1323u, start = h->ago > AG_WINDOW ? AG_WINDOW : h->ago;
        uint32_t c = h->lane ? AG_BLUE : AG_AMBER;
        int32_t y, x0, x1, fade;
        if (h->ago > AG_WINDOW + len)
            continue;
        y = AG_Y(h->note);
        fade = 256 - (int32_t)(start * 200u / AG_WINDOW);
        x0 = AG_X(start);
        x1 = AG_X(h->ago > len ? h->ago - len : 0);
        fill(x0, y - 1, x1 - x0 > 1 ? x1 - x0 : 2, 3, ag_mix(c, fade * 3 / 4));
        if (h->ago < len) {
            spot(x1, y, 5, ag_mix(c, 60));
            spot(x1, y, 3, c);
        } else {
            spot(x0, y, 1, ag_mix(c, fade));
        }
    }
#undef AG_X
#undef AG_Y
}

/* Under the sky: how the lanes relate, in the lanes' colours, and how the keys fill the pool. */
static void ag_draw_status(void)
{
    char t[32] = {0}, n[12];
    int duo = value(AP_DUO) != 0;
    if (duo)
        upper(ap_relation_name(value(AP_RELATION)), t, sizeof t);
    else
        text_cat(t, "A ALONE");
    ag_spaced(10, 150, &FONT_XS, t, duo ? AG_BLUE : AG_AMBER, 2, 0);
    upper(ap_hold_name(value(AP_HOLD_MODE)), t, sizeof t);
    if (value(AP_HOLD_MODE) == AP_RING) {
        text_cat(t, " ");
        int_text(ag_pool.count, n);
        text_cat(t, n);
        text_cat(t, "/");
        int_text(ag_pool.ring, n);
        text_cat(t, n);
    }
    text_cat(t, "  ");
    if (ag_playing) {
        int_text(value(AP_TEMPO), n);
        text_cat(t, n);
        text_cat(t, " BPM");
    } else {
        text_cat(t, "STOPPED");
    }
    ag_spaced(230, 152, &FONT_T, t, AG_FADED, 1, 1);
}

static uint32_t ag_accent(int p) { return p == 0 ? AG_AMBER : p == 1 ? AG_BLUE : AG_ROSE; }

static void ag_draw_tabs(void)
{
    int i;
    for (i = 0; i < M->npages; i++) {
        int32_t x = 3 + i * 47;
        int focused = page == i;
        if (focused) {
            fill(x, 168, 45, 26, AG_PALE);
            fill(x + 6, 191, 33, 2, ag_accent(i));
        }
        text_at(x + 22, 174, &FONT_XS, M->page_title[i], focused ? AG_INK : AG_FADED, 1);
    }
}

/* Each value: its name, the value, and a track with a spot where it sits, greyed where it does nothing. */
static void ag_draw_chips(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        const md_param_t *p = &M->params[page][i];
        int32_t x = 3 + i * 59, y = 200;
        int v = value(p->field), lo, hi, on = ag_applies(p->field);
        char s[16] = {0}, name[16];
        if (i > 0)
            fill(x - 2, y + 4, 1, 29, AG_PALE);
        upper(p->name, name, sizeof name);
        text_at(x + 28, y + 1, &FONT_T, name, AG_FADED, 1);
        format(p->fmt, v, s);
        text_at(x + 28, y + 11, text_w(&FONT_B, s) > 54 ? &FONT_XS : &FONT_B, s, on ? AG_INK : AG_FADED, 1);
        ap_range(p->field, &lo, &hi);
        fill(x + 7, y + 31, 42, 1, AG_PALE);
        spot(x + 7 + (v - lo) * 42 / (hi - lo > 0 ? hi - lo : 1), y + 31, 2, on ? ag_accent(page) : AG_FADED);
    }
}

static void ag_draw(uint32_t now)
{
    char t[24];
    (void)now;
    fill(0, band_y, 240, BAND_H, AG_NIGHT);
    ag_draw_stars();
    spot(13, 13, 4, AG_AMBER);
    spot(18, 13, 4, AG_BLUE);
    upper(ap_scene(item())->title, t, sizeof t);
    ag_spaced(30, 5, &FONT_B, t, AG_INK, 2, 0);
    draw_battery(207, 9, 1, AG_INK);
    fill(9, 27, 222, 1, AG_PALE);
    ag_draw_strip();
    ag_draw_sky();
    ag_draw_status();
    ag_draw_tabs();
    if (ui.pill[0]) {
        fill(14, 204, 212, 28, AG_PALE);
        fill(22, 229, 196, 2, AG_ROSE);
        text_at(120, 209, &FONT_B, ui.pill, AG_INK, 1);
    } else {
        ag_draw_chips();
    }
}

static int ag_expect(const char *what, int *got)
{
    uint32_t f;
    int i;
    if (isc_streq(what, "scene"))
        *got = item();
    else if (isc_streq(what, "relation"))
        *got = value(AP_RELATION);
    else if (isc_streq(what, "duo"))
        *got = value(AP_DUO);
    else if (isc_streq(what, "hold"))
        *got = value(AP_HOLD_MODE);
    else if (isc_streq(what, "playing"))
        *got = ag_playing;
    else if (isc_streq(what, "writes"))
        *got = ag_writes;
    else if (isc_streq(what, "pool")) {          /* the notes in the pool */
        ApPool p;
        f = plat_lock();
        ap_player_pool(&p);
        plat_unlock(f);
        *got = p.count;
    } else if (isc_streq(what, "held")) {        /* the notes the keys and the lanes hold */
        f = plat_lock();
        for (i = 0, *got = 0; i < 128; i++)
            *got += note_holds[i] != 0;
        plat_unlock(f);
    } else if (isc_streq(what, "releases")) {    /* the lanes' notes waiting for their release */
        f = plat_lock();
        *got = ap_player_held();
        plat_unlock(f);
    } else
        return 0;
    return 1;
}

static const md_theme_t AG_THEME = {
    .bg = AG_NIGHT, .ink = AG_INK, .faded = AG_FADED, .tab_bg = AG_NIGHT, .tab_ink = AG_FADED, .focus_bg = AG_PALE,
    .focus_ink = AG_INK, .chip_bg = AG_NIGHT, .chip_frame = AG_PALE, .chip_ink = AG_INK, .bar_off = AG_PALE,
    .pill_bg = AG_PALE, .pill_ink = AG_INK, .dots = 0, .tab_frame = 0,
};

static const md_mode_t MODE_ARPEGGI = {
    .name = "Arpeggi",
    .reset_words = "as written",
    .obj = OBJ_ARPEGGI,
    .magic = 0x31475041u, /* "APG1" */
    .version = 1,
    .fields = AP_FIELDS,
    .npages = 5,
    .page_title = {"A", "B", "NOTES", "SOUND", "TIME"},
    .params = {{{"Shape", AP_A_SHAPE, AG_SHAPE}, {"Rate", AP_A_RATE, AG_RATE}, {"Rhythm", AP_A_RHYTHM, AG_RHYTHM},
                {"Gate", AP_A_GATE, AG_PERCENT}},
               {{"Shape", AP_B_SHAPE, AG_SHAPE}, {"Rate", AP_B_RATE, AG_RATE}, {"Rhythm", AP_B_RHYTHM, AG_RHYTHM},
                {"Gate", AP_B_GATE, AG_PERCENT}},
               {{"Hold", AP_HOLD_MODE, AG_HOLD}, {"Ring", AP_RING_SIZE, FMT_NUM}, {"Span", AP_SPAN, AG_OCT},
                {"B Pitch", AP_B_PITCH, AG_SIGNED}},
               {{"Sound", AP_SOUND, AG_SOUND}, {"Bright", AP_BRIGHT, FMT_NUM}, {"Decay", AP_DECAY, FMT_NUM},
                {"Room", AP_ROOM, FMT_NUM}},
               {{"Tempo", AP_TEMPO, FMT_NUM}, {"Swing", AP_SWING, AG_PERCENT}, {"Echo", AP_ECHO, FMT_NUM},
                {"Echo Steps", AP_ECHO_STEPS, AG_STEPS}}},
    .algo_field = AP_RELATION,
    .algo_name = "Relation",
    .algo_fmt = AG_RELATION,
    .theme = &AG_THEME,
    .extra = ag_state,
    .extra_len = sizeof ag_state,
    .count = ap_scene_count,
    .defaults = ap_default_scene,
    .range = ap_range,
    .clamp = ag_clamp,
    .voice = ap_voice,
    .fx = ap_fx,
    .pattern = ag_pattern,
    .key_notes = ag_key_notes,
    .mono = ag_mono,
    .releases = ag_releases,
    .format = ag_format,
    .item_name = ag_item_name,
    .press = ag_press,
    .knob = ag_knob,
    .dim = ag_dim,
    .frame = ag_frame,
    .start = ag_start,
    .selected = ag_selected,
    .draw = ag_draw,
    .idle_ms = 1000,
    .busy_ms = 1000,
    .expect = ag_expect,
    .block = ap_player_block,
    .busy = ag_busy,
    .lit = ag_lit,
    .key_up = ag_key_up,
    .save_press = ag_clear,
};
