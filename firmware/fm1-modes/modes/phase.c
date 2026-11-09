/* Phase as a mode: process music, a short cell played by two to four parts that drift apart, jump, fill in, follow in
 * canon, pulse, stretch or loop. Its pieces and sounds are firmware/phase/dsp's port of the desktop's PhaseCore, its
 * processes are PhaseCore's own process.c, and its player runs them in the audio interrupt (all checked by
 * firmware/phase/tests/run_parity.sh). This file is the panel, the phrase the keys write and the graphic score on the
 * screen. Included by app.c. */
#include "ph.h"
#include "player.h"

enum { PM_PROCESS = 1, PM_MOTION, PM_REPEATS, PM_PERCENT, PM_SIGNED, PM_SOUND, PM_SPAN };

#define PM_INK 0x141414u
#define PM_PAPER 0xF3F0E8u
#define PM_RED 0xD7261Eu
#define PM_FADED 0x9A958Cu
#define PM_PALE 0xD6D1C6u
#define PM_PHRASE_MS 1500u               /* how long the keys must rest before a phrase becomes the cell */
#define PM_FRAME_MS 50u                  /* the score turns at 20 frames a second while it plays */
#define PM_CX 120                        /* the centre of the rings */
#define PM_CY 92
#define PM_OUTER 55.f

/* Whether the keys write the cell (saved with the pieces), and each piece's cell (saved after them). */
static uint8_t pm_state[1] = {1};
#define pm_writes pm_state[0]
static PhaseCell pm_cells[MD_ITEMS];

static uint8_t pm_playing, pm_restart;
static PhaseProc pm_sent;                /* what the player was last given */
static PhaseView pm_view;                /* where the process is, for the screen and the lights */
static struct {
    int8_t note;
    uint32_t at;
} pm_phrase[PH_MAX_STEPS];               /* the phrase being written, and when each key went down */
static int pm_nphrase;
static uint32_t pm_last_press;
static char pm_motion[8] = "Drift";      /* the Motion knob's name, which is the process's own */

/* The player's way to the engine: the core's note counts, which the keys share. */
int ph_refs(int note) { return md_refs(note); }
void ph_hold(int note, int strike) { md_hold(note, strike); }
void ph_let_go(int note) { md_let_go(note); }

/* ------------------------------------------------------------------ the process */

/* The process the piece, its cell and the octave make: the octave moves the keys and the piece together. */
static void pm_proc(PhaseProc *p)
{
    phase_proc_make(row, &pm_cells[item()], p);
    p->transpose += octave * 12;
}

static int pm_same(const PhaseProc *a, const PhaseProc *b)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    unsigned i;
    for (i = 0; i < sizeof *a; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

static void pm_play(void)
{
    uint32_t f;
    pm_proc(&pm_sent);
    f = plat_lock();
    ph_player_start(&pm_sent);
    plat_unlock(f);
    pm_playing = 1;
    ui.redraw = 1;
}

static void pm_stop(void)
{
    uint32_t f = plat_lock();
    ph_player_stop();
    plat_unlock(f);
    pm_playing = 0;
    ui.redraw = 1;
}

/* ------------------------------------------------------------------ the phrase the keys write */

/* The phrase as a cell, as the desktop makes it. The step is the time between most presses, a little under the
 * middle one, and each gap is that many steps, so a pause becomes a rest or two. */
static int pm_cell_of_phrase(int8_t out[PH_MAX_STEPS])
{
    uint32_t gaps[PH_MAX_STEPS], sorted[PH_MAX_STEPS], unit;
    int ng = pm_nphrase - 1, n = 0, i, j, steps;
    if (pm_nphrase <= 0)
        return 0;
    for (i = 0; i < ng; i++)
        gaps[i] = sorted[i] = pm_phrase[i + 1].at - pm_phrase[i].at;
    for (i = 1; i < ng; i++)
        for (j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) {
            uint32_t t = sorted[j];
            sorted[j] = sorted[j - 1];
            sorted[j - 1] = t;
        }
    unit = ng ? sorted[(ng - 1) * 3 / 10] : 250u;
    unit = unit < 60u ? 60u : unit > 1000u ? 1000u : unit;
    out[n++] = pm_phrase[0].note;
    for (i = 0; i < ng; i++) {
        steps = clamp((int)((2u * gaps[i] + unit) / (2u * unit)), 1, 4);
        for (j = 1; j < steps && n < PH_MAX_STEPS; j++)
            out[n++] = -1;
        if (n < PH_MAX_STEPS)
            out[n++] = pm_phrase[i + 1].note;
    }
    if (n < 2)
        out[n++] = -1;
    return n;
}

/* The phrase becomes the cell: Length is set to it, and the process starts with it. */
static void pm_finish(void)
{
    int8_t notes[PH_MAX_STEPS];
    PhaseCell *c = &pm_cells[item()];
    char t[24] = {0}, num[8];
    int n = pm_cell_of_phrase(notes), i, count = 0;
    pm_nphrase = 0;
    if (!n)
        return;
    isc_memset(c->note, -1, sizeof c->note);
    for (i = 0; i < n; i++) {
        c->note[i] = notes[i];
        count += notes[i] >= 0;
    }
    c->length = (int8_t)n;
    row[PH_LENGTH] = n;
    row[PH_DENSITY] = 100;
    touched();
    pm_play();
    int_text(count, num);
    text_cat(t, num);
    text_cat(t, count == 1 ? " note, " : " notes, ");
    int_text(n, num);
    text_cat(t, num);
    text_cat(t, " steps");
    show_pill("New cell", t);
}

/* A key going down: while the keys write, it adds a note to the phrase, and the first stops the process so the
 * phrase is heard alone. Every key plays its own note. */
static int pm_key_notes(const int32_t *s, int note, int left, int out[8])
{
    (void)s, (void)left;
    if (pm_writes) {
        if (!pm_nphrase && pm_playing)
            pm_stop();
        if (pm_nphrase < PH_MAX_STEPS) {
            pm_phrase[pm_nphrase].note = (int8_t)(note - octave * 12);
            pm_phrase[pm_nphrase].at = plat_ms();
            pm_nphrase++;
        }
        pm_last_press = plat_ms();
        ui.redraw = 1;
    }
    out[0] = note;
    return 1;
}

/* How far the wait for the phrase to end has gone, in thousandths. */
static int pm_phrase_wait(uint32_t now)
{
    uint32_t w;
    if (!pm_nphrase || ui.keys)
        return 0;
    w = (now - pm_last_press) * 1000u / PM_PHRASE_MS;
    return w > 1000u ? 1000 : (int)w;
}

/* ------------------------------------------------------------------ the mode's side of the core */

static const char *const PM_MOTION_NAMES[PH_PROCESS_COUNT] = {"Drift", "Jump",   "Offset", "Gap",
                                                              "Swell", "Growth", "Drift",  "Drift"};

static void pm_name_motion(void)
{
    pm_motion[0] = 0;
    text_cat(pm_motion, PM_MOTION_NAMES[clamp(value(PH_PROCESS), 0, PH_PROCESS_COUNT - 1)]);
}

/* As written, and the cell with it; a piece put back while playing starts again. */
static void pm_defaults(int piece, int32_t *out)
{
    ph_default_piece(piece, out);
    ph_default_cell(piece, &pm_cells[piece]);
    pm_restart = 1;
}

/* The rows are saved a byte a field, so a negative transpose comes back as a byte over 127. */
static void pm_clamp(int32_t *s)
{
    if (s[PH_TRANSPOSE] > 127)
        s[PH_TRANSPOSE] -= 256;
    ph_clamp(s);
}

static void pm_pattern(const int32_t *s, md_pattern_t *p)
{
    p->on = 0, p->mode = 0, p->octaves = 1, p->rate = 0, p->gate = 50, p->tempo = s[PH_TEMPO];
}

static int pm_mono(const int32_t *s) { return (void)s, 0; }
static int pm_releases(int field) { return (void)field, 0; }
static const char *pm_item_name(int i) { return ph_piece(i)->title; }

static void pm_format(int fmt, int v, char *out)
{
    char n[12];
    switch (fmt) {
    case PM_PROCESS: text_cat(out, ph_process_name(v)); break;
    case PM_MOTION: {
        int p = value(PH_PROCESS);
        text_cat(out, p == PH_PHASE || p == PH_RESULT || p == PH_TAPE ? "1/"
                      : p == PH_SHIFT                                 ? "+"
                      : p == PH_BUILD || p == PH_CANON                ? "-"
                                                                      : "");
        int_text(phase_motion_steps(p, v, value(PH_LENGTH)), n);
        text_cat(out, n);
        break;
    }
    case PM_REPEATS:
        int_text(v, n);
        text_cat(out, "x");
        text_cat(out, n);
        break;
    case PM_PERCENT:
        int_text(v, n);
        text_cat(out, n);
        text_cat(out, "%");
        break;
    case PM_SIGNED:
        int_text(v, n);
        text_cat(out, v > 0 ? "+" : "");
        text_cat(out, n);
        break;
    case PM_SOUND: text_cat(out, ph_sound_name(v)); break;
    case PM_SPAN: text_cat(out, ph_spread_name(v)); break;
    }
}

static void pm_press(uint32_t b)
{
    switch (b) {
    case B_LFO: cycle(PH_PARTS, "Parts", FMT_NUM); break;
    case B_EDIT: cycle(PH_SPREAD, "Span", PM_SPAN); break;
    case B_GLO:
        pm_play();
        show_pill("From the beginning", 0);
        break;
    case B_PLAY:
        if (pm_playing) {
            pm_stop();
            show_pill("Stopped", 0);
        } else {
            pm_play();
            show_pill(ph_piece(item())->title, 0);
        }
        break;
    case B_REC:
        pm_writes = !pm_writes;
        if (!pm_writes)
            pm_nphrase = 0;
        mark_dirty();
        show_pill(pm_writes ? "Keys write the cell" : "Keys play along", 0);
        break;
    }
}

/* As the desktop's knobs: a step of two only on the long ranges. */
static int pm_knob(int k, int delta)
{
    const md_param_t *p = &M->params[page][k];
    char t[16];
    int lo, hi;
    ph_range(p->field, &lo, &hi);
    set_field(p->field, value(p->field) + delta * (hi - lo + 1 > 40 ? 2 : 1));
    format(p->fmt, value(p->field), t);
    show_pill(p->name, t);
    return 1;
}

/* PLAY is lit at the top of each step of part 1 and dim between; REC is lit while the keys write, blinking while a
 * phrase is written. */
static int pm_beat(void) { return pm_playing && pm_view.part[0].pos_milli < 400; }

static uint32_t pm_lit(void)
{
    uint32_t b = pm_beat() ? 1u << B_PLAY : 0u;
    if (pm_writes && (!pm_nphrase || plat_ms() / 133u % 2u == 0u))
        b |= 1u << B_REC;
    return b;
}

static uint32_t pm_dim(void) { return pm_playing && !pm_beat() ? 1u << B_PLAY : 0u; }
static int pm_busy(void) { return pm_playing; }

/* Every start is a stopped one, with each piece's cell whole. */
static void pm_start(void)
{
    uint32_t f;
    int i, j, ok;
    pm_playing = pm_restart = 0;
    pm_nphrase = 0;
    f = plat_lock();
    ph_player_reset();
    plat_unlock(f);
    for (i = 0; i < ph_piece_count(); i++) {
        PhaseCell *c = &pm_cells[i];
        ok = c->length >= 2 && c->length <= PH_MAX_STEPS;
        for (j = 0; j < PH_MAX_STEPS; j++)
            ok &= c->note[j] >= -1;
        if (!ok)
            ph_default_cell(i, c);
    }
}

/* A new piece: the phrase is dropped, and a playing process goes on with the new piece from its start. */
static void pm_selected(void)
{
    pm_nphrase = 0;
    pm_name_motion();
    if (pm_playing)
        pm_play();
}

static void pm_frame(uint32_t now)
{
    PhaseProc p;
    int64_t tick = 0;
    uint32_t f;
    if (pm_restart) {
        pm_restart = 0;
        pm_nphrase = 0;
        if (pm_playing)
            pm_play();
    }
    if (pm_nphrase) {
        if (ui.keys)
            pm_last_press = now;
        else if (now - pm_last_press >= PM_PHRASE_MS)
            pm_finish();
    }
    /* Any change reaches the player: a change to how the parts move restarts it on the next step. */
    pm_proc(&p);
    if (pm_playing && !pm_same(&p, &pm_sent)) {
        pm_sent = p;
        f = plat_lock();
        ph_player_change(&pm_sent);
        plat_unlock(f);
    }
    if (pm_playing) {
        f = plat_lock();
        ph_player_now(&tick);
        plat_unlock(f);
    }
    phase_view(pm_playing ? &pm_sent : &p, tick, &pm_view);
    pm_name_motion();
    if ((pm_playing || pm_nphrase) && now - ui.drawn_at >= PM_FRAME_MS)
        ui.redraw = 1;
}

/* ------------------------------------------------------------------ the screen */

static float pm_radius(int q, int parts)
{
    float gap;
    if (parts <= 1)
        return PM_OUTER;
    gap = 33.f / (float)(parts - 1);
    return PM_OUTER - (float)q * (gap < 16.f ? gap : 16.f);
}

/* The angle a step of a ring sits at, with the play position at the top, under the red line. */
static float pm_angle(float step, float pos, int length)
{
    return -FM_PI / 2.f + 2.f * FM_PI * (step - pos) / (float)length;
}

static void pm_text_right(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint32_t rgb)
{
    text_at(x - text_w(f, s), y, f, s, rgb, 0);
}

/* Text with its letters spaced out, as the desktop's titles and labels are. */
static int32_t pm_spaced_w(const felucca_font_t *f, const char *s, int32_t gap)
{
    char c[2] = {0, 0};
    int32_t w = 0;
    for (; *s; s++) {
        c[0] = *s;
        w += text_w(f, c) + (s[1] ? gap : 0);
    }
    return w;
}

static void pm_spaced(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint32_t rgb, int32_t gap,
                      int centre)
{
    char c[2] = {0, 0};
    if (centre)
        x -= pm_spaced_w(f, s, gap) / 2;
    for (; *s; s++) {
        c[0] = *s;
        text_at(x, y, f, c, rgb, 0);
        x += text_w(f, c) + gap;
    }
}

/* A round spot: a little fuller than the core's, as the desktop's dots are. */
static void pm_dot(int32_t cx, int32_t cy, int32_t r, uint32_t rgb)
{
    int32_t dy;
    for (dy = -r; dy <= r; dy++) {
        int32_t dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r)
            dx++;
        fill(cx - dx, cy + dy, dx * 2 + 1, 1, rgb);
    }
}

/* Under the rings: the process in red, how it stands, and the figure for it (the offset, the fill, the stage). */
static void pm_draw_status(void)
{
    const PhaseView *v = &pm_view;
    char fig[16] = {0}, state[24] = {0}, n[12];
    int process = value(PH_PROCESS), length = v->length, on = 0, q, times = 0;
    int stage = v->stage, stages = v->stages;
    for (q = 0; q < v->parts; q++)
        on += v->part[q].active;
    switch (process) {
    case PH_PHASE:
    case PH_RESULT:
    case PH_TAPE:
    case PH_SHIFT: {
        int off = v->offset_milli;
        text_cat(fig, "+");
        if (v->drifting) {
            int tenths = (off + 50) / 100;
            int_text(tenths / 10, n);
            text_cat(fig, n);
            text_cat(fig, ".");
            int_text(tenths % 10, n);
        } else {
            int_text(off / 1000, n);
        }
        text_cat(fig, n);
        text_cat(state, process == PH_TAPE ? "sliding" : v->drifting ? "drifting" : off == 0 ? "unison" : "locked");
        break;
    }
    case PH_CANON:
        int_text(v->parts > 1 ? -v->lead_milli / 1000 : 0, n);
        text_cat(fig, v->parts > 1 ? "-" : "");
        text_cat(fig, n);
        int_text(on, n);
        text_cat(state, n);
        text_cat(state, " of ");
        int_text(v->parts, n);
        text_cat(state, n);
        text_cat(state, " in");
        break;
    case PH_BUILD: {
        int rests = stages / 2;
        text_cat(fig, "+");
        int_text(stage <= rests ? stage : stages - stage, n);
        text_cat(fig, n);
        length = rests;
        text_cat(state, stage < rests ? "filling" : "emptying");
        break;
    }
    case PH_PULSE:
        int_text(stage + 1, n);
        text_cat(fig, n);
        length = stages > 1 ? stages : 1;
        int_text(on, n);
        text_cat(state, n);
        text_cat(state, " of ");
        int_text(v->parts, n);
        text_cat(state, n);
        text_cat(state, " sounding");
        break;
    default: {
        int gate = value(PH_GATE) * 10, tenths;
        gate = gate > 0 ? gate : 1;
        tenths = (v->part[0].len_milli[0] * 10 + gate / 2) / gate;
        if (tenths < 100) {
            int_text(tenths / 10, n);
            text_cat(fig, n);
            text_cat(fig, ".");
            int_text(tenths % 10, n);
        } else {
            int_text((tenths + 5) / 10, n);
        }
        text_cat(fig, n);
        times = 1;
        length = 0;
        text_cat(state, stage < 8 ? "growing" : "shrinking");
    }
    }
    if (length) {
        int_text(length, n);
        text_cat(fig, "/");
        text_cat(fig, n);
    }
    if (!pm_playing) {
        state[0] = 0;
        text_cat(state, "stopped");
    }
    upper(state, state, sizeof state);
    upper(ph_process_name(process), n, sizeof n);
    pm_spaced(10, 138, &FONT_B, n, PM_RED, 2, 0);
    pm_spaced(10, 154, &FONT_T, state, PM_FADED, 1, 0);
    pm_text_right(230, 141, &FONT_M, fig, PM_INK);
    /* the font has no times sign: a small cross before the figure */
    if (times) {
        int32_t x = 230 - text_w(&FONT_M, fig) - 10, y = 150;
        segment(x, y, x + 6, y + 6, 2, PM_INK);
        segment(x + 6, y, x, y + 6, 2, PM_INK);
    }
}

/* The score: each part a ring of its cell's steps, turning under the fixed red line that is now. A rest is a small
 * mark, a note a spot a little further out the higher it is, a long note an arc, and the notes sounding are red. */
static void pm_draw_score(void)
{
    const PhaseView *v = &pm_view;
    int parts = v->parts, length = v->length < 2 ? 2 : v->length, q, i;
    float inner = pm_radius(parts - 1, parts);
    fill(PM_CX, PM_CY - 64, 1, (int32_t)(PM_OUTER + 18.f - inner + 0.5f), PM_RED);
    for (q = 0; q < parts; q++) {
        const PhasePartView *pv = &v->part[q];
        float r = pm_radius(q, parts), pos = (float)pv->pos_milli / 1000.f, mid = 60.f, half = 6.f, a0;
        int active = pv->active, lo = 127, hi = 0;
        uint32_t ink = active ? PM_INK : PM_PALE;
        for (i = 0; i < length; i++)
            if (pv->note[i] >= 0) {
                lo = pv->note[i] < lo ? pv->note[i] : lo;
                hi = pv->note[i] > hi ? pv->note[i] : hi;
            }
        if (lo <= hi) {
            mid = (float)(lo + hi) / 2.f;
            half = (float)(hi - lo) / 2.f > 6.f ? (float)(hi - lo) / 2.f : 6.f;
        }
        ring(PM_CX, PM_CY, (int32_t)(r + 0.5f), ink, active ? 0 : 2);
        /* the first step: a bar across the ring, so how far the rings have turned against each other shows */
        a0 = pm_angle(0.f, pos, length);
        segment(polar_x(PM_CX, a0, r - 5.f), polar_y(PM_CY, a0, r - 5.f), polar_x(PM_CX, a0, r + 5.f),
                polar_y(PM_CY, a0, r + 5.f), 1, ink);
        for (i = 0; i < length; i++) {
            float a = pm_angle((float)i, pos, length), rr, t;
            int n = pv->note[i], into, lit;
            int32_t len = pv->len_milli[i];
            uint32_t col;
            if (n < 0) {
                fill(polar_x(PM_CX, a, r) - 1, polar_y(PM_CY, a, r) - 1, 2, 2, active ? PM_FADED : PM_PALE);
                continue;
            }
            t = ((float)n - mid) / half;
            rr = r + (t < -1.f ? -1.f : t > 1.f ? 1.f : t) * 2.5f;
            /* sounding: the play line is inside the note */
            into = pv->pos_milli - i * 1000;
            if (into < 0)
                into += length * 1000;
            lit = active && pm_playing && into < (len > 150 ? len : 150);
            col = !active ? PM_PALE : lit ? PM_RED : PM_INK;
            if (len > 550) {
                float end = (float)len / 1000.f < (float)length - 0.2f ? (float)len / 1000.f : (float)length - 0.2f;
                arc(PM_CX, PM_CY, rr, a, pm_angle((float)i + end, pos, length), 2, col);
            }
            pm_dot(polar_x(PM_CX, a, rr), polar_y(PM_CY, a, rr), lit ? 3 : 2, col);
        }
    }
    if (!pm_playing) {
        /* stopped: a play sign in the middle */
        int32_t dy;
        for (dy = -6; dy <= 6; dy++)
            fill(PM_CX - 4, PM_CY + dy, 1 + (6 - (dy < 0 ? -dy : dy)) * 10 / 6, 1, PM_INK);
    }
    pm_draw_status();
}

/* While a phrase is written: the cell it will become, its notes as stems on a line in their steps, the newest in
 * red, and a red bar running out as the wait for the phrase to end goes by. */
static void pm_draw_phrase(uint32_t now)
{
    int8_t ns[PH_MAX_STEPS];
    int n = pm_cell_of_phrase(ns), last = 0, i;
    char t[8], num[4];
    const int32_t x0 = 20, w = 200, base = 104;
    for (i = 0; i < n; i++)
        if (ns[i] >= 0)
            last = i;
    pm_spaced(10, 37, &FONT_B, "WRITING", PM_RED, 2, 0);
    int_text(n, t);
    text_cat(t, "/");
    int_text(PH_MAX_STEPS, num);
    text_cat(t, num);
    pm_text_right(230, 39, &FONT_XS, t, PM_FADED);
    fill(x0, base, w, 1, PM_INK);
    for (i = 0; i < PH_MAX_STEPS; i++) {
        int32_t x = x0 + (2 * i + 1) * w / (2 * PH_MAX_STEPS), y, rise;
        uint32_t col = i == last ? PM_RED : PM_INK;
        if (i >= n || ns[i] < 0) {
            fill(x, base + 3, 1, 3, i < n ? PM_INK : PM_PALE);
            continue;
        }
        rise = clamp((ns[i] - 60) * 17 / 10, -30, 44);
        y = base - rise;
        fill(x, y < base ? y : base, 1, rise < 0 ? -rise : rise, col);
        pm_dot(x, y, 3, col);
    }
    fill(x0, 146, w * (1000 - pm_phrase_wait(now)) / 1000, 3, PM_RED);
    pm_spaced(120, 153, &FONT_T, "THE PHRASE BECOMES THE CELL", PM_FADED, 1, 1);
}

static void pm_draw_tabs(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        int32_t x = 3 + i * 59;
        int focused = page == i;
        if (focused) {
            fill(x, 168, 56, 26, PM_INK);
            fill(x, 168, 4, 26, PM_RED);
        } else {
            fill(x, 168, 56, 1, PM_INK);
        }
        pm_spaced(x + 28 + (focused ? 2 : 0), 174, &FONT_XS, M->page_title[i], focused ? PM_PAPER : PM_INK, 1, 1);
    }
}

/* Each value: its name, the value, and a track with a red square where it sits. */
static void pm_draw_chips(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        const md_param_t *p = &M->params[page][i];
        int32_t x = 3 + i * 59, y = 200;
        int v = value(p->field), lo, hi;
        char s[16], name[16];
        if (i > 0)
            fill(x - 2, y + 4, 1, 29, PM_PALE);
        upper(p->name, name, sizeof name);
        text_at(x + 28, y + 1, &FONT_T, name, PM_FADED, 1);
        format(p->fmt, v, s);
        text_at(x + 28, y + 11, text_w(&FONT_B, s) > 54 ? &FONT_XS : &FONT_B, s, PM_INK, 1);
        ph_range(p->field, &lo, &hi);
        fill(x + 7, y + 31, 42, 1, PM_INK);
        fill(x + 7 + (v - lo) * 42 / (hi - lo > 0 ? hi - lo : 1) - 2, y + 29, 5, 5, PM_RED);
    }
}

static void pm_draw_pill(void)
{
    fill(14, 204, 212, 28, PM_INK);
    fill(14, 204, 6, 28, PM_RED);
    text_at(123, 210, &FONT_B, ui.pill, PM_PAPER, 1);
}

static void pm_draw(uint32_t now)
{
    char t[24];
    fill(0, band_y, 240, BAND_H, PM_PAPER);
    pm_dot(13, 13, 4, PM_RED);
    upper(ph_piece(item())->title, t, sizeof t);
    pm_spaced(25, 5, &FONT_B, t, PM_INK, 2, 0);
    draw_battery(207, 9, 1, PM_INK);
    fill(9, 27, 222, 1, PM_INK);
    if (pm_nphrase)
        pm_draw_phrase(now);
    else
        pm_draw_score();
    pm_draw_tabs();
    if (ui.pill[0])
        pm_draw_pill();
    else
        pm_draw_chips();
}

static int pm_expect(const char *what, int *got)
{
    uint32_t f;
    int i;
    if (isc_streq(what, "piece"))
        *got = item();
    else if (isc_streq(what, "process"))
        *got = value(PH_PROCESS);
    else if (isc_streq(what, "parts"))
        *got = value(PH_PARTS);
    else if (isc_streq(what, "length"))
        *got = value(PH_LENGTH);
    else if (isc_streq(what, "transpose"))
        *got = value(PH_TRANSPOSE);
    else if (isc_streq(what, "playing"))
        *got = pm_playing;
    else if (isc_streq(what, "writes"))
        *got = pm_writes;
    else if (isc_streq(what, "writing"))         /* the notes of the phrase being written */
        *got = pm_nphrase;
    else if (isc_streq(what, "cell"))            /* the steps of the piece's cell */
        *got = pm_cells[item()].length;
    else if (isc_streq(what, "held")) {          /* the notes the keys and the parts hold */
        f = plat_lock();
        for (i = 0, *got = 0; i < 128; i++)
            *got += note_holds[i] != 0;
        plat_unlock(f);
    } else if (isc_streq(what, "releases")) {    /* the parts' notes waiting for their release */
        f = plat_lock();
        *got = ph_player_held();
        plat_unlock(f);
    } else
        return 0;
    return 1;
}

static const md_theme_t PM_THEME = {
    .bg = PM_PAPER, .ink = PM_INK, .faded = PM_FADED, .tab_bg = PM_PAPER, .tab_ink = PM_INK, .focus_bg = PM_INK,
    .focus_ink = PM_PAPER, .chip_bg = PM_PAPER, .chip_frame = PM_INK, .chip_ink = PM_INK, .bar_off = PM_PALE,
    .pill_bg = PM_INK, .pill_ink = PM_PAPER, .dots = 0, .tab_frame = 0,
};

static const md_mode_t MODE_PHASE = {
    .name = "Phase",
    .reset_words = "as written",
    .obj = OBJ_PHASE,
    .magic = 0x31534850u, /* "PHS1" */
    .version = 1,
    .fields = PH_FIELDS,
    .npages = 4,
    .page_title = {"PROCESS", "CELL", "SOUND", "TIME"},
    .params = {{{"Process", PH_PROCESS, PM_PROCESS}, {pm_motion, PH_MOTION, PM_MOTION}, {"Parts", PH_PARTS, FMT_NUM},
                {"Repeats", PH_REPEATS, PM_REPEATS}},
               {{"Length", PH_LENGTH, FMT_NUM}, {"Density", PH_DENSITY, PM_PERCENT},
                {"Transpose", PH_TRANSPOSE, PM_SIGNED}, {"Gate", PH_GATE, PM_PERCENT}},
               {{"Sound", PH_SOUND, PM_SOUND}, {"Bright", PH_BRIGHT, FMT_NUM}, {"Decay", PH_DECAY, FMT_NUM},
                {"Room", PH_ROOM, FMT_NUM}},
               {{"Tempo", PH_TEMPO, FMT_NUM}, {"Echo", PH_ECHO, FMT_NUM}, {"Echo Steps", PH_ECHO_STEPS, FMT_NUM},
                {"Span", PH_SPREAD, PM_SPAN}}},
    .algo_field = PH_PROCESS,
    .algo_name = "Process",
    .algo_fmt = PM_PROCESS,
    .theme = &PM_THEME,
    .extra = pm_state,
    .extra_len = sizeof pm_state,
    .more = (uint8_t *)pm_cells,
    .more_len = sizeof pm_cells,
    .count = ph_piece_count,
    .defaults = pm_defaults,
    .range = ph_range,
    .clamp = pm_clamp,
    .voice = ph_voice,
    .fx = ph_fx,
    .pattern = pm_pattern,
    .key_notes = pm_key_notes,
    .mono = pm_mono,
    .releases = pm_releases,
    .format = pm_format,
    .item_name = pm_item_name,
    .press = pm_press,
    .knob = pm_knob,
    .dim = pm_dim,
    .frame = pm_frame,
    .start = pm_start,
    .selected = pm_selected,
    .draw = pm_draw,
    .idle_ms = 1000,
    .busy_ms = 1000,
    .expect = pm_expect,
    .block = ph_player_block,
    .busy = pm_busy,
    .lit = pm_lit,
};
