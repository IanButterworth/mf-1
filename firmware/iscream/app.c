/* iScream on the FM-1: the panel, the sundae it edits and the screen that draws it, as in the desktop app.
 * Part of fm1-base's unity build, after the platform: its drawing and screen reader are in scope. The sound is
 * made by the engine in dsp/, which runs only in the audio interrupt. */
#include "fastmath.h"
#include "isc_libc.h"
#include "iscream.h"
#include "iscream.c"
#include "synth.h"
#ifdef PLAT_HOST
#include <stdlib.h>
#endif

#define NFLAVORS ISC_FLAVOR_COUNT
#define KEY_VELOCITY 90                  /* the FM-1's keys have no velocity; this is its global default */

/* ------------------------------------------------------------------ the model */

enum { F_SCOOP, F_CONE, F_CREAM, F_FUDGE, F_CARAMEL, F_CHIPS, F_SPRINKLES, NFOCUS };
static const char *const FOCUS_TITLE[NFOCUS] = {"Scoop", "Cone", "Whipped Cream", "Hot Fudge", "Caramel",
                                                "Choc Chips", "Sprinkles"};
static const char *const FOCUS_SHORT[NFOCUS] = {"Scoop", "Cone", "Cream", "Fudge", "Caramel", "Chips", "Sprinkles"};

enum { FMT_NUM, FMT_PCT, FMT_STYLE, FMT_TOAST, FMT_ROOM, FMT_ONOFF };
typedef struct {
    const char *name;
    int field, fmt;
} param_t;

#define STORE_MAGIC 0x31435349u          /* "ISC1" */
typedef struct {
    uint32_t magic;
    uint8_t version, flavor, focus;
    int8_t octave;
    uint8_t sundae[NFLAVORS][ISC_FIELDS];
} stored_t;

static int32_t table[NFLAVORS][ISC_FIELDS];   /* every flavor keeps how it was left */
static int32_t sundae[ISC_FIELDS];            /* the one being played */
static int focus;
static int octave;
static uint8_t dirty;
static uint32_t changed_at;

static struct {
    char pill[40];
    uint32_t pill_until;
    uint8_t redraw;
    uint32_t keys, btn;
    int32_t key_note[NKEYS];
    uint32_t save_down_at;
    uint8_t save_long;
    uint8_t oct_both;
    uint32_t drawn_at;
    int8_t battery;
    uint8_t low_battery;
} ui;

static int flavor(void) { return sundae[ISC_FLAVOR]; }

static int value(int field) { return sundae[field]; }

static int switch_field(int f)
{
    if (f == F_SCOOP)
        return -1;
    return f == F_CONE ? ISC_CONE_ON : ISC_TOP(f - F_CREAM, 0);
}

static int is_on(int f) { return f == F_SCOOP || value(switch_field(f)) != 0; }

static void params(int f, param_t out[4])
{
    static const char *const NAMES[5][3] = {{"Mix", "Rate", "Depth"}, {"Gain", "Tone", "Level"},
                                            {"Drizzle", "Rate", "Depth"}, {"Mix", "Echo", "Speed"},
                                            {"Mix", "Decay", "Room"}};
    int k;
    if (f == F_SCOOP) {
        out[0] = (param_t){"Sweet", ISC_SWEET, FMT_NUM};
        out[1] = (param_t){"Melt", ISC_MELT, FMT_NUM};
        out[2] = (param_t){"Fizz", ISC_FIZZ, FMT_NUM};
        out[3] = (param_t){"Scoops", ISC_SCOOPS, FMT_NUM};
    } else if (f == F_CONE) {
        out[0] = (param_t){"Style", ISC_CONE_STYLE, FMT_STYLE};
        out[1] = (param_t){"Toast", ISC_TOAST, FMT_TOAST};
        out[2] = (param_t){"Crunch", ISC_CRUNCH, FMT_NUM};
        out[3] = (param_t){"Cone", ISC_CONE_ON, FMT_ONOFF};
    } else {
        int t = f - F_CREAM;
        for (k = 0; k < 3; k++)
            out[k] = (param_t){NAMES[t][k], ISC_TOP(t, k + 1), FMT_PCT};
        if (t == ISC_SPRINKLES)
            out[2].fmt = FMT_ROOM;
        out[3] = (param_t){FOCUS_SHORT[f], ISC_TOP(t, 0), FMT_ONOFF};
    }
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void format(const param_t *p, int v, char *out)
{
    static const char *const STYLES[3] = {"Waffle", "Sugar", "Cup"};
    static const char *const ROOMS[3] = {"Room", "Hall", "Plate"};
    out[0] = 0;
    switch (p->fmt) {
    case FMT_PCT:
        int_text(v, out);
        text_cat(out, "%");
        break;
    case FMT_STYLE: text_cat(out, STYLES[clamp(v, 0, 2)]); break;
    case FMT_ROOM: text_cat(out, ROOMS[clamp(v, 0, 2)]); break;
    case FMT_ONOFF: text_cat(out, v ? "On" : "Off"); break;
    case FMT_TOAST: {
        /* the cutoff in Hz, as the cone's toast reads it */
        int hz = (int)(100.f * fm_powf(200.f, (float)v / 107.f) + 0.5f);
        if (hz >= 1000) {
            char tenths[4] = {'.', 0, 'k', 0};
            tenths[1] = (char)('0' + (hz % 1000) / 100);
            int_text(hz / 1000, out);
            text_cat(out, tenths);
        } else {
            int_text(hz, out);
        }
        break;
    }
    default: int_text(v, out);
    }
}

static void show_pill(const char *name, const char *v)
{
    ui.pill[0] = 0;
    text_cat(ui.pill, name);
    if (v) {
        text_cat(ui.pill, ": ");
        text_cat(ui.pill, v);
    }
    ui.pill_until = plat_ms() + 1200u;
    ui.redraw = 1;
    if (ann_on())
        ann_say(ui.pill, 0, ANN_NOW);
}

void app_say(const char *a, const char *b) { show_pill(a, b && b[0] ? b : 0); }

static int battery_level(void)
{
#ifdef PLAT_HOST
    const char *s = getenv("ISCREAM_BATTERY");   /* the simulator's battery is always full: a test can set another */
    if (s && s[0] >= '0' && s[0] <= '4')
        return s[0] - '0';
#endif
    return plat_battery();
}

/* As on the FM-1: bars at the top right, and LOW BATTERY over the whole screen at the last bar, until the
 * battery is charged or USB powers the FM-1. */
static void battery(void)
{
    int b = battery_level();
    if (b == ui.battery)
        return;
    ui.battery = (int8_t)b;
    ui.redraw = 1;
    if ((b == 0) != ui.low_battery) {
        ui.low_battery = b == 0;
        if (ui.low_battery && ann_on())
            ann_say("LOW BATTERY", 0, ANN_NOW);
    }
}

/* ------------------------------------------------------------------ the engine's side */

/* The screen's loop hands the engine a new voice and effects, and the keys' notes; the audio interrupt takes
 * them before its next block. The loop writes with the interrupts held off, so the interrupt never sees half. */
static struct {
    uint8_t voice[155];
    uint8_t mono;
    isc_fx_params_t fx;
} pend;
static volatile uint8_t pend_voice, pend_fx, pend_panic;
static volatile uint16_t note_ring[64];
static volatile uint32_t note_head, note_tail;
static volatile int32_t pend_program = -1;
static volatile float peak_level;

static void engine_apply(void)
{
    uint8_t voice[155];
    isc_fx_params_t fx;
    uint32_t f;
    isc_voice(sundae, voice);
    isc_fx(sundae, &fx);
    f = plat_lock();
    isc_memcpy(pend.voice, voice, sizeof voice);
    pend.mono = isc_flavor(flavor())->mono;
    pend.fx = fx;
    pend_voice = pend_fx = 1;
    plat_unlock(f);
}

static void engine_note(int note, int on)
{
    uint32_t f = plat_lock();
    if (note_head - note_tail < 64u) {
        note_ring[note_head & 63u] = (uint16_t)(note | (on ? 0x100 : 0));
        note_head++;
    }
    plat_unlock(f);
}

static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        switch (st) {
        case 0x90u: isc_synth_note_on((int)d1, (int)d2); break;
        case 0x80u: isc_synth_note_off((int)d1); break;
        case 0xB0u:
            if (d1 == 64u)
                isc_synth_sustain(d2 >= 64u);
            else if (d1 == 1u)
                isc_synth_mod_wheel((int)d2);
            else if (d1 == 120u || d1 == 123u)
                isc_synth_all_notes_off();
            break;
        case 0xD0u: isc_synth_aftertouch((int)d1); break;
        case 0xE0u: isc_synth_pitch_bend((int)(d1 | d2 << 7)); break;
        case 0xC0u: pend_program = (int32_t)d1; break;
        }
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 / 4096.f * 8388607.f, pk = 0;
    uint32_t i = 0, k;
    if (pend_panic) {
        isc_synth_all_notes_off();
        pend_panic = 0;
    }
    if (pend_voice) {
        isc_synth_set_voice(pend.voice, pend.mono);
        pend_voice = 0;
    }
    if (pend_fx) {
        isc_synth_set_fx(&pend.fx);
        pend_fx = 0;
    }
    while (note_tail != note_head) {
        uint16_t e = note_ring[note_tail & 63u];
        if (e & 0x100u)
            isc_synth_note_on(e & 0x7F, KEY_VELOCITY);
        else
            isc_synth_note_off(e & 0x7F);
        note_tail++;
    }
    midi_in();
    while (i < n) {
        float x[FM_N];
        isc_synth_block(x);
        for (k = 0; k < FM_N && i < n; k++, i++) {
            float s = x[k];
            float a = s < 0 ? -s : s;
            int32_t v;
            if (a > pk)
                pk = a;
            s = s > 1.f ? 1.f : s < -1.f ? -1.f : s;
            v = (int32_t)(s * gain);
            lr[2 * i] = v;
            lr[2 * i + 1] = v;
        }
    }
    peak_level = pk > peak_level ? pk : peak_level * 0.9f;
}

void app_panic(void) { pend_panic = 1; }

/* ------------------------------------------------------------------ editing */

static void touched(void)
{
    isc_clamp(sundae);
    isc_memcpy(table[flavor()], sundae, sizeof sundae);
    engine_apply();
    dirty = 1;
    changed_at = plat_ms();
    ui.redraw = 1;
}

static void set_field(int field, int v)
{
    int lo, hi;
    isc_range(field, &lo, &hi);
    v = clamp(v, lo, hi);
    if (sundae[field] == v)
        return;
    sundae[field] = v;
    touched();
}

static void select_flavor(int i)
{
    int next = ((i % NFLAVORS) + NFLAVORS) % NFLAVORS;
    if (next == flavor())
        return;
    isc_memcpy(table[flavor()], sundae, sizeof sundae);
    isc_memcpy(sundae, table[next], sizeof sundae);
    sundae[ISC_FLAVOR] = next;
    touched();
}

static void fresh_scoop(void)
{
    int f = flavor();
    isc_default_sundae(f, sundae);
    touched();
    show_pill("Fresh scoop", 0);
}

static void toggle(int f)
{
    int field = switch_field(f), on;
    if (field < 0)
        return;
    on = value(field) == 0;
    set_field(field, on);
    show_pill(FOCUS_TITLE[f], on ? "On" : "Off");
}

static void set_focus(int f)
{
    if (f == focus)
        return;
    focus = f;
    dirty = 1;
    changed_at = plat_ms();
    ui.redraw = 1;
}

static void turn_knob(int k, int delta)
{
    param_t ps[4], *p;
    char t[16];
    int lo, hi, sw;
    params(focus, ps);
    p = &ps[k];
    if (p->fmt == FMT_ONOFF) {
        int want = delta > 0;
        if (value(p->field) != want) {
            set_field(p->field, want);
            show_pill(FOCUS_TITLE[focus], want ? "On" : "Off");
        }
        return;
    }
    /* A knob on something that is off switches it on, so there is no menu to dive for. */
    sw = switch_field(focus);
    if (sw >= 0 && value(sw) == 0)
        set_field(sw, 1);
    isc_range(p->field, &lo, &hi);
    set_field(p->field, value(p->field) + delta * (hi - lo + 1 > 12 ? 2 : 1));
    format(p, value(p->field), t);
    show_pill(p->name, t);
}

static void octave_to(int o)
{
    char t[4];
    o = clamp(o, -3, 3);
    octave = o;
    t[0] = o > 0 ? '+' : o < 0 ? '-' : ' ';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    show_pill("Octave", o ? t : "0");
    dirty = 1;
    changed_at = plat_ms();
}

/* The top row of buttons picks the cone or a topping, HOME the scoop. A second press switches it. */
static int focus_for_button(uint32_t b)
{
    switch (b) {
    case B_HOME: return F_SCOOP;
    case B_FX: return F_CONE;
    case B_SEL: return F_CREAM;
    case B_ENV: return F_FUDGE;
    case B_LFO: return F_CARAMEL;
    case B_EDIT: return F_CHIPS;
    case B_GLO: return F_SPRINKLES;
    }
    return -1;
}

static void press(uint32_t b)
{
    int f = focus_for_button(b);
    if (f >= 0) {
        if (focus == f)
            toggle(f);
        else
            set_focus(f);
        return;
    }
    switch (b) {
    case B_ARP: select_flavor(flavor() - 1); break;
    case B_SEQ: select_flavor(flavor() + 1); break;
    case B_SAVE:
        ui.save_down_at = plat_ms();
        ui.save_long = 0;
        break;
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), k, now = plat_ms();
    int32_t e;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            int32_t note = 53 + (int32_t)k + 12 * octave;
            if (note < 0 || note > 127)
                continue;
            ui.key_note[k] = note;
            engine_note((int)note, 1);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | (uint32_t)KEY_VELOCITY << 24, TO_USB);
        } else if (!(keys & bit) && (ui.keys & bit) && ui.key_note[k] >= 0) {
            engine_note((int)ui.key_note[k], 0);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)ui.key_note[k] << 16, TO_USB);
            ui.key_note[k] = -1;
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if ((btn & bit) && !(ui.btn & bit) && k != B_OCTDN && k != B_OCTUP)
            press(k);
    }
    /* SAVE is RESET here: held, a fresh scoop; pressed, a reminder. */
    if ((btn >> B_SAVE & 1u) && !ui.save_long && now - ui.save_down_at >= 700u) {
        ui.save_long = 1;
        fresh_scoop();
    }
    if (!(btn >> B_SAVE & 1u) && (ui.btn >> B_SAVE & 1u) && !ui.save_long)
        show_pill("Hold RESET for a fresh scoop", 0);
    /* OCT- and OCT+ step on release; both together put the octave back. */
    if ((btn >> B_OCTDN & 1u) && (btn >> B_OCTUP & 1u) && !ui.oct_both) {
        ui.oct_both = 1;
        octave_to(0);
    }
    for (k = B_OCTDN; k <= B_OCTUP; k++)
        if (!(btn >> k & 1u) && (ui.btn >> k & 1u)) {
            if (!ui.oct_both)
                octave_to(octave + (k == B_OCTUP ? 1 : -1));
            if (!(btn >> B_OCTDN & 1u) && !(btn >> B_OCTUP & 1u))
                ui.oct_both = 0;
        }
    for (k = 0; k < NE; k++) {
        char t[8];
        if ((e = plat_enc((int)k)) == 0)
            continue;
        switch (k) {
        case EN_SELECT: set_focus(clamp(focus + (e > 0 ? 1 : -1), 0, NFOCUS - 1)); break;
        case EN_PRESET: select_flavor(flavor() + (e > 0 ? 1 : -1)); break;
        case EN_ALGO:
            set_field(ISC_SCOOPS, value(ISC_SCOOPS) + (e > 0 ? 1 : -1));
            int_text(value(ISC_SCOOPS), t);
            show_pill("Scoops", t);
            break;
        default: turn_knob((int)(k - EN_K1), (int)e);
        }
    }
    if (pend_program >= 0) {
        int32_t p = pend_program;
        pend_program = -1;
        if (p < NFLAVORS)
            select_flavor((int)p);
    }
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

static void lights(void)
{
    uint32_t lit = 0, dim = 0, keys = 0, b;
    int k;
    for (b = 0; b < NB; b++) {
        int f = focus_for_button(b);
        if (f < 0)
            continue;
        if (f == focus)
            lit |= 1u << b;
        else if (f != F_SCOOP && is_on(f))
            dim |= 1u << b;
    }
    if (octave < 0)
        lit |= 1u << B_OCTDN;
    if (octave > 0)
        lit |= 1u << B_OCTUP;
    for (k = 0; k < NKEYS; k++) {
        int note = 53 + k + 12 * octave;
        if ((ui.keys >> k & 1u) || (note >= 0 && note < 128 && isc_synth_sounding(note)))
            keys |= 1u << k;
    }
    plat_lights(lit, keys, dim, 0, 1);
}

/* ------------------------------------------------------------------ keeping the sundaes */

static void load(void)
{
    static stored_t st;
    int f, i;
    for (f = 0; f < NFLAVORS; f++)
        isc_default_sundae(f, table[f]);
    focus = F_SCOOP;
    octave = 0;
    f = 0;
    if (plat_store_load(OBJ_SETTINGS, &st, sizeof st) == (int)sizeof st && st.magic == STORE_MAGIC && st.version == 1) {
        for (f = 0; f < NFLAVORS; f++)
            for (i = 0; i < ISC_FIELDS; i++)
                table[f][i] = st.sundae[f][i];
        for (f = 0; f < NFLAVORS; f++) {
            table[f][ISC_FLAVOR] = f;
            isc_clamp(table[f]);
        }
        focus = clamp(st.focus, 0, NFOCUS - 1);
        octave = clamp(st.octave, -3, 3);
        f = clamp(st.flavor, 0, NFLAVORS - 1);
    }
    isc_memcpy(sundae, table[f], sizeof sundae);
}

/* A flash write silences the sound for a moment, so it waits until nothing plays. */
static void save_when_quiet(void)
{
    static stored_t st;
    int f, i;
    if (!dirty || plat_ms() - changed_at < 1500u || isc_synth_active_voices() || ui.keys)
        return;
    isc_memcpy(table[flavor()], sundae, sizeof sundae);
    st.magic = STORE_MAGIC;
    st.version = 1;
    st.flavor = (uint8_t)flavor();
    st.focus = (uint8_t)focus;
    st.octave = (int8_t)octave;
    for (f = 0; f < NFLAVORS; f++)
        for (i = 0; i < ISC_FIELDS; i++)
            st.sundae[f][i] = (uint8_t)table[f][i];
    if (plat_store_save(OBJ_SETTINGS, &st, sizeof st) == 0)
        dirty = 0;
    else
        changed_at = plat_ms();
}

void app_init(void)
{
    int k;
    isc_synth_init();
    load();
    engine_apply();
    for (k = 0; k < NKEYS; k++)
        ui.key_note[k] = -1;
}

int app_expect(const char *what, int *got)
{
    if (isc_streq(what,"flavor"))
        *got = flavor();
    else if (isc_streq(what,"focus"))
        *got = focus;
    else if (isc_streq(what,"scoops"))
        *got = value(ISC_SCOOPS);
    else if (isc_streq(what,"sweet"))
        *got = value(ISC_SWEET);
    else if (isc_streq(what,"cone"))
        *got = value(ISC_CONE_ON);
    else if (isc_streq(what,"battery"))
        *got = ui.battery;
    else if (isc_streq(what,"low_battery"))
        *got = ui.low_battery;
    else if (isc_streq(what,"voices"))
        *got = isc_synth_active_voices();
    else if (isc_streq(what,"octave"))
        *got = octave;
    else if (isc_streq(what,"dirty"))
        *got = dirty;
    else
        return 0;
    return 1;
}

#include "screen.c"
