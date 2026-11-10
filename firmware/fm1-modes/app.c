/* MF-1's modes on the FM-1 in one firmware: the shared core, which owns the engine, the keys, the arpeggiator, saving
 * and the panel, and runs whichever mode was chosen at power-on. Hold a piano key while switching on for the chooser;
 * otherwise the last mode used starts. Part of fm1-base's unity build, after the platform: its drawing and screen
 * reader are in scope. The sound is made by iScream's engine port, which runs only in the audio interrupt. */
#include "fastmath.h"
#include "isc_libc.h"
#include "synth.h"
#include "mode.h"
#ifdef PLAT_HOST
#include <stdlib.h>
#endif

#define KEY_VELOCITY 90                  /* the FM-1's keys have no velocity; this is its global default */
#define SCENE_MS 166u                    /* the scenes move at six frames a second */

/* ------------------------------------------------------------------ the modes */

static const md_mode_t *const *modes(int *n);
static const md_mode_t *M;               /* the mode running, or none while the chooser is up */
static int mode_index;

/* The chooser's keys: the white keys from F3, so each mode has one to itself. */
static const uint8_t CHOOSER_KEY[8] = {0, 2, 4, 6, 7, 9, 11, 12};
static const char *const CHOOSER_NOTE[8] = {"F", "G", "A", "B", "C", "D", "E", "F"};

/* ------------------------------------------------------------------ the model */

static int32_t table[MD_ITEMS][MD_FIELDS];   /* every item keeps how it was left */
static int32_t row[MD_FIELDS];               /* the one being played; field 0 is the item */
static int page;
static int octave;
static uint8_t dirty, global_dirty;
static uint32_t changed_at;

static struct {
    char pill[40];
    uint32_t pill_until;
    uint8_t redraw;
    uint32_t keys, btn;
    int8_t key_notes[NKEYS][8];    /* the notes each held key plays */
    uint8_t key_count[NKEYS];
    uint8_t note_refs[128];        /* how many held keys play each note */
    uint32_t save_down_at;
    uint8_t save_long;
    uint8_t oct_both;
    uint32_t drawn_at;
    uint32_t touched_at;           /* the last button or knob, the piano keys aside */
    int8_t battery;
    uint8_t low_battery;
    uint8_t chooser, chooser_wait;     /* the chooser is up; it waits for the keys held at power-on to be let go */
} ui;

static int item(void) { return row[0]; }
static int value(int field) { return row[field]; }

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

static void format(int fmt, int v, char *out)
{
    out[0] = 0;
    if (fmt == FMT_NUM)
        int_text(v, out);
    else
        M->format(fmt, v, out);
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
    const char *s = getenv("MODES_BATTERY");     /* the simulator's battery is always full: a test can set another */
    if (s && s[0] >= '0' && s[0] <= '4')
        return s[0] - '0';
#endif
    return plat_battery();
}

/* As on the FM-1: bars at the top right from the FM-1's own reading, and LOW BATTERY over the whole screen at the
 * last bar, until the battery is charged or USB powers the FM-1. */
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

/* The screen's loop hands the engine a new voice, effects and arpeggio, and the keys' notes; the audio interrupt takes
 * them before its next block. The loop writes with the interrupts held off, so the interrupt never sees half. */
static struct {
    uint8_t voice[155];
    uint8_t mono;
    isc_fx_params_t fx;
    md_pattern_t pattern;
} pend;
static volatile uint8_t pend_voice, pend_fx, pend_pattern, pend_panic;
static volatile uint16_t note_ring[64];
static volatile uint32_t note_head, note_tail;
static volatile int32_t pend_program = -1;
static volatile float peak_level;

static void engine_apply(void)
{
    uint8_t voice[155];
    isc_fx_params_t fx;
    md_pattern_t pattern;
    uint32_t f;
    M->voice(row, voice);
    M->fx(row, &fx);
    M->pattern(row, &pattern);
    f = plat_lock();
    isc_memcpy(pend.voice, voice, sizeof voice);
    pend.mono = (uint8_t)M->mono(row);
    pend.fx = fx;
    pend.pattern = pattern;
    pend_voice = pend_fx = pend_pattern = 1;
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

/* For a mode that plays notes of its own: how many hold each note, the keys and the mode's parts together, and the
 * mode's player, which the start of the mode hands the audio interrupt. Only the interrupt counts. */
static uint8_t note_holds[128];
static void (*volatile mode_block)(void);

static int md_refs(int note) { return note_holds[note & 127]; }

/* One more holder of a note. A note no one held always sounds; strike sounds a held one again. */
static void md_hold(int note, int strike)
{
    note &= 127;
    if (strike || note_holds[note] == 0)
        isc_synth_note_on(note, KEY_VELOCITY);
    if (note_holds[note] < 255)
        note_holds[note]++;
}

static void md_let_go(int note)
{
    note &= 127;
    if (note_holds[note] && --note_holds[note] == 0)
        isc_synth_note_off(note);
}

/* The arpeggiator player: MF-1's arpeggiator, run by the audio interrupt on the engine's 64-sample blocks. Held notes
 * go to it instead of the voices while a repeat, chase or pattern plays; the first step is at once, and each note is released at
 * the gate, a block early, so the release never lands after the block the next step fires in. */
static struct {
    md_pattern_t cfg;
    uint8_t held[32];
    int nheld, index, running;
    uint32_t rng;
    float phase, step_len;
    int off_note;
    float off_left;
} arp = {.rng = 1, .off_note = -1};

static float arp_step_len(void)
{
    static const float WHOLE[10] = {1.f,         0.5f,          0.25f,  0.25f * 2 / 3,  0.125f,
                                    0.125f * 2 / 3, 0.0625f, 0.0625f * 2 / 3, 0.03125f, 0.03125f * 2 / 3};
    int tempo = arp.cfg.tempo < 30 ? 30 : arp.cfg.tempo > 300 ? 300 : arp.cfg.tempo;
    return WHOLE[clamp(arp.cfg.rate, 0, 9)] * 4.f * 60.f / (float)tempo * 44100.f;
}

static void arp_release(void)
{
    if (arp.off_note >= 0)
        isc_synth_note_off(arp.off_note);
    arp.off_note = -1;
}

static void arp_reset(void)
{
    arp_release();
    arp.nheld = 0;
    arp.running = 0;
}

static void arp_hold(int note, int on)
{
    int i;
    for (i = 0; i < arp.nheld && arp.held[i] != note; i++)
        ;
    if (on) {
        if (i < arp.nheld || arp.nheld >= 32)
            return;
        arp.held[arp.nheld++] = (uint8_t)note;
        if (!arp.running) {
            arp.index = 0;
            arp.phase = 0;
            arp.running = 1;
        }
    } else if (i < arp.nheld) {
        isc_memmove(&arp.held[i], &arp.held[i + 1], (unsigned)(arp.nheld - i - 1));
        if (--arp.nheld == 0)
            arp.running = 0;
    }
}

/* MF-1's modes: 0 up, 1 down, 2 inclusive, 3 exclusive, 4 random, 5 in the order played, 6 each note twice. */
static int arp_next(void)
{
    uint8_t sorted[32];
    int n = arp.nheld, total, idx = 0, i, j;
    if (n == 0)
        return -1;
    for (i = 0; i < n; i++)
        sorted[i] = arp.held[i];
    if (arp.cfg.mode != 5)
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) {
                uint8_t t = sorted[j];
                sorted[j] = sorted[j - 1];
                sorted[j - 1] = t;
            }
    total = n * clamp(arp.cfg.octaves, 1, 4);
    switch (arp.cfg.mode) {
    case 1:
        idx = total - 1 - arp.index % total;
        arp.index = (arp.index + 1) % total;
        break;
    case 2: {
        int cycle = total * 2, k = arp.index % cycle;
        idx = k < total ? k : cycle - 1 - k;
        arp.index = (arp.index + 1) % cycle;
        break;
    }
    case 3: {
        int cycle = total * 2 - 2 > 1 ? total * 2 - 2 : 1, k = arp.index % cycle;
        idx = total == 1 ? 0 : k < total ? k : cycle - k;
        arp.index = (arp.index + 1) % cycle;
        break;
    }
    case 4:
        arp.rng = arp.rng * 1664525u + 1013904223u;
        idx = (int)((arp.rng >> 16) % (uint32_t)total);
        break;
    case 6:
        idx = arp.index / 2 % total;
        arp.index = (arp.index + 1) % (total * 2);
        break;
    default:
        idx = arp.index % total;
        arp.index = (arp.index + 1) % total;
    }
    return sorted[idx % n] + 12 * (idx / n);
}

static void arp_block(void)
{
    if (arp.off_note >= 0 && (arp.off_left -= (float)FM_N) <= 0)
        arp_release();
    if (!arp.running)
        return;
    while (arp.phase < (float)FM_N && arp.running) {
        float len = arp_step_len(), gate = len * (float)clamp(arp.cfg.gate, 20, 100) / 100.f;
        int note = arp_next();
        if (note >= 0 && note <= 127) {
            arp_release();
            isc_synth_note_on(note, KEY_VELOCITY);
            arp.off_note = note;
            arp.off_left = gate - (float)FM_N > 64.f ? gate - (float)FM_N : 64.f;
        }
        arp.phase += len;
    }
    arp.phase -= (float)FM_N;
}

static void play_note(int note, int on)
{
    if (arp.cfg.on)
        arp_hold(note, on);
    else if (on)
        isc_synth_note_on(note, KEY_VELOCITY);
    else
        isc_synth_note_off(note);
}

static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        switch (st) {
        case 0x90u:
            if (d2)
                play_note((int)d1, 1);
            else
                play_note((int)d1, 0);
            break;
        case 0x80u: play_note((int)d1, 0); break;
        case 0xB0u:
            if (d1 == 64u)
                isc_synth_sustain(d2 >= 64u);
            else if (d1 == 1u)
                isc_synth_mod_wheel((int)d2);
            else if (d1 == 120u || d1 == 123u) {
                arp_reset();
                isc_synth_all_notes_off();
            }
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
    void (*block)(void) = mode_block;
    if (pend_panic) {
        arp_reset();
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
    if (pend_pattern) {
        if (pend.pattern.on != arp.cfg.on)
            arp_reset();
        arp.cfg = pend.pattern;
        pend_pattern = 0;
    }
    while (note_tail != note_head) {
        uint16_t e = note_ring[note_tail & 63u];
        if (block && !arp.cfg.on) {
            if (e & 0x100u)
                md_hold(e & 0x7F, 1);
            else
                md_let_go(e & 0x7F);
        } else {
            play_note(e & 0x7F, (e & 0x100u) != 0);
        }
        note_tail++;
    }
    midi_in();
    while (i < n) {
        float x[FM_N];
        arp_block();
        if (block)
            block();
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

/* ------------------------------------------------------------------ keys */

/* The lowest twelve keys are the left hand, which a split gives the drone. */
static void key_down(uint32_t k)
{
    int notes[8], n, i, root = 53 + (int)k + 12 * octave;
    if (root < 0 || root > 127)
        return;
    n = M->key_notes(row, root, k < 12u, notes);
    ui.key_count[k] = (uint8_t)n;
    for (i = 0; i < n; i++) {
        ui.key_notes[k][i] = (int8_t)notes[i];
        if (ui.note_refs[notes[i]]++ == 0) {
            engine_note(notes[i], 1);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)notes[i] << 16 | (uint32_t)KEY_VELOCITY << 24, TO_USB);
        }
    }
}

/* A note two keys share stops only when both are up. */
static void key_up(uint32_t k)
{
    int i;
    for (i = 0; i < ui.key_count[k]; i++) {
        int note = ui.key_notes[k][i];
        if (ui.note_refs[note] && --ui.note_refs[note] == 0) {
            engine_note(note, 0);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)note << 16, TO_USB);
        }
    }
    ui.key_count[k] = 0;
}

static void release_all_keys(void)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        key_up(k);
}

/* ------------------------------------------------------------------ editing */

static void touched(void)
{
    M->clamp(row);
    isc_memcpy(table[item()], row, sizeof row);
    engine_apply();
    dirty = 1;
    changed_at = plat_ms();
    ui.redraw = 1;
}

static void set_field(int field, int v)
{
    int lo, hi;
    M->range(field, &lo, &hi);
    v = clamp(v, lo, hi);
    if (row[field] == v)
        return;
    /* A new playing style must not leave notes of the old one hanging. */
    if (M->releases(field))
        release_all_keys();
    row[field] = v;
    touched();
}

static void select_item(int i)
{
    int n = M->count(), next = ((i % n) + n) % n;
    if (next == item())
        return;
    release_all_keys();
    isc_memcpy(table[item()], row, sizeof row);
    isc_memcpy(row, table[next], sizeof row);
    row[0] = next;
    touched();
    if (M->selected)
        M->selected();
}

static void reset_item(void)
{
    char t[40] = {0};
    release_all_keys();
    M->defaults(item(), row);
    touched();
    if (M->reset_pill) {
        text_cat(t, M->reset_pill);
    } else {
        text_cat(t, M->item_name(item()));
        text_cat(t, " ");
        text_cat(t, M->reset_words);
    }
    show_pill(t, 0);
}

static void mark_dirty(void)
{
    dirty = 1;
    changed_at = plat_ms();
}

static void set_page(int p)
{
    if (p == page)
        return;
    page = p;
    mark_dirty();
    ui.redraw = 1;
}

static void turn_knob(int k, int delta)
{
    const md_param_t *p = &M->params[page][k];
    char t[16];
    int lo, hi;
    if (M->knob && M->knob(k, delta))
        return;
    M->range(p->field, &lo, &hi);
    set_field(p->field, value(p->field) + delta * (hi - lo + 1 > 12 ? 2 : 1));
    format(p->fmt, value(p->field), t);
    show_pill(p->name, t);
}

/* Steps a short list round, for the buttons a mode gives to its styles, voicings and the like. */
static void cycle(int field, const char *name, int fmt)
{
    char t[16];
    int lo, hi;
    M->range(field, &lo, &hi);
    set_field(field, value(field) >= hi ? lo : value(field) + 1);
    format(fmt, value(field), t);
    show_pill(name, t);
}

static void octave_to(int o)
{
    char t[4];
    o = clamp(o, -3, 3);
    release_all_keys();
    octave = o;
    t[0] = o > 0 ? '+' : o < 0 ? '-' : ' ';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    show_pill("Octave", o ? t : "0");
    mark_dirty();
}

/* HOME is the first page, then the top row's buttons from FX, as many as the mode has pages. */
static int page_for_button(uint32_t b)
{
    static const uint8_t PAGE_BUTTON[MD_PAGES] = {B_HOME, B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO};
    int i;
    for (i = 0; i < M->npages; i++)
        if (PAGE_BUTTON[i] == b)
            return i;
    return -1;
}

static void press(uint32_t b)
{
    int p = page_for_button(b);
    if (p >= 0) {
        if (p == page && M->repress)
            M->repress(p);
        else
            set_page(p);
        return;
    }
    switch (b) {
    case B_ARP: select_item(item() - 1); break;
    case B_SEQ: select_item(item() + 1); break;
    case B_SAVE:
        ui.save_down_at = plat_ms();
        ui.save_long = 0;
        break;
    default: M->press(b);
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), k, now = plat_ms();
    int32_t e;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit))
            key_down(k);
        else if (!(keys & bit) && (ui.keys & bit))
            key_up(k);
    }
    if (btn != ui.btn)
        ui.touched_at = now;
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if ((btn & bit) && !(ui.btn & bit) && k != B_OCTDN && k != B_OCTUP)
            press(k);
    }
    /* SAVE is RESET here: held, the item as it ships; pressed, a reminder. */
    if ((btn >> B_SAVE & 1u) && !ui.save_long && now - ui.save_down_at >= 700u) {
        ui.save_long = 1;
        reset_item();
    }
    if (!(btn >> B_SAVE & 1u) && (ui.btn >> B_SAVE & 1u) && !ui.save_long) {
        char t[48] = {0};
        text_cat(t, "Hold RESET for ");
        text_cat(t, M->reset_words);
        show_pill(t, 0);
    }
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
        char t[16];
        if ((e = plat_enc((int)k)) == 0)
            continue;
        ui.touched_at = now;
        switch (k) {
        case EN_SELECT: set_page(clamp(page + (e > 0 ? 1 : -1), 0, M->npages - 1)); break;
        case EN_PRESET: select_item(item() + (e > 0 ? 1 : -1)); break;
        case EN_ALGO:
            set_field(M->algo_field, value(M->algo_field) + (e > 0 ? 1 : -1));
            format(M->algo_fmt, value(M->algo_field), t);
            show_pill(M->algo_name, t);
            break;
        default: turn_knob((int)(k - EN_K1), (int)e);
        }
    }
    if (pend_program >= 0) {
        int32_t p = pend_program;
        pend_program = -1;
        if (p < M->count())
            select_item((int)p);
    }
    /* Keys held across a change of style or octave were released; they play again when pressed again. */
    ui.keys = keys;
    ui.btn = btn;
}

static void lights(void)
{
    uint32_t lit = 0, dim = 0, keys = 0, b;
    int k;
    if (ui.chooser) {
        int n;
        modes(&n);
        for (k = 0; k < n && k < 8; k++)
            keys |= 1u << CHOOSER_KEY[k];
        plat_lights(0, keys, 0, 0, 1);
        return;
    }
    for (b = 0; b < NB; b++)
        if (page_for_button(b) == page)
            lit |= 1u << b;
    dim = M->dim();
    if (M->lit)
        lit |= M->lit();
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

/* ------------------------------------------------------------------ keeping each mode's items */

/* Each mode keeps its rows in a store object of its own, so a change to one mode's format never touches another's.
 * The settings object holds which mode ran last. */
#define GLOBAL_MAGIC 0x31444F4Du         /* "MOD1" */
typedef struct {
    uint32_t magic;
    uint8_t version;
    uint32_t last;                       /* the last mode's magic */
} global_t;

typedef struct {
    uint32_t magic;
    uint8_t version, item, page, items, fields;
    int8_t octave;
    uint8_t extra[8];
    uint8_t rows[MD_ITEMS][MD_FIELDS];
} stored_t;

/* The object as it is loaded and saved: a mode's further state follows the rows, so a mode without any keeps the size
 * it always had. */
static struct {
    stored_t st;
    uint8_t more[MD_MORE_MAX];
} store_buf;

static uint32_t last_mode;

static void load_global(void)
{
    global_t g;
    last_mode = 0;
    if (plat_store_load(OBJ_SETTINGS, &g, sizeof g) == (int)sizeof g && g.magic == GLOBAL_MAGIC && g.version == 1)
        last_mode = g.last;
}

static void load_mode(void)
{
    stored_t *const st = &store_buf.st;
    uint32_t size = sizeof(stored_t) + M->more_len;
    int n = M->count(), it, i;
    for (it = 0; it < n; it++)
        M->defaults(it, table[it]);
    page = 0;
    octave = 0;
    it = 0;
    if (plat_store_load(M->obj, &store_buf, size) == (int)size && st->magic == M->magic &&
        st->version == M->version && st->items == n && st->fields == M->fields) {
        for (it = 0; it < n; it++) {
            for (i = 0; i < M->fields; i++)
                table[it][i] = st->rows[it][i];
            table[it][0] = it;
            M->clamp(table[it]);
        }
        page = clamp(st->page, 0, M->npages - 1);
        octave = clamp(st->octave, -3, 3);
        it = clamp(st->item, 0, n - 1);
        if (M->extra_len)
            isc_memcpy(M->extra, st->extra, M->extra_len);
        if (M->more_len)
            isc_memcpy(M->more, store_buf.more, M->more_len);
    }
    isc_memcpy(row, table[it], sizeof row);
}

/* A flash write silences the sound for a moment, so it waits until nothing plays. */
static void save_when_quiet(void)
{
    stored_t *const st = &store_buf.st;
    int it, i;
    if (!(dirty || global_dirty) || plat_ms() - changed_at < 1500u || isc_synth_active_voices() || ui.keys ||
        arp.running || (M->busy && M->busy()))
        return;
    if (global_dirty) {
        global_t g;
        isc_memset(&g, 0, sizeof g);
        g.magic = GLOBAL_MAGIC;
        g.version = 1;
        g.last = M->magic;
        if (plat_store_save(OBJ_SETTINGS, &g, sizeof g) == 0)
            global_dirty = 0;
    }
    if (dirty) {
        isc_memcpy(table[item()], row, sizeof row);
        isc_memset(st, 0, sizeof *st);
        st->magic = M->magic;
        st->version = M->version;
        st->item = (uint8_t)item();
        st->page = (uint8_t)page;
        st->items = (uint8_t)M->count();
        st->fields = (uint8_t)M->fields;
        st->octave = (int8_t)octave;
        if (M->extra_len)
            isc_memcpy(st->extra, M->extra, M->extra_len);
        for (it = 0; it < M->count(); it++)
            for (i = 0; i < M->fields; i++)
                st->rows[it][i] = (uint8_t)table[it][i];
        if (M->more_len)
            isc_memcpy(store_buf.more, M->more, M->more_len);
        if (plat_store_save(M->obj, &store_buf, sizeof(stored_t) + M->more_len) == 0)
            dirty = 0;
    }
    if (dirty || global_dirty)
        changed_at = plat_ms();
}

/* ------------------------------------------------------------------ starting a mode */

static void start_mode(int i)
{
    int n;
    uint32_t f;
    const md_mode_t *const *all = modes(&n);
    mode_index = clamp(i, 0, n - 1);
    M = all[mode_index];
    load_mode();
    ui.chooser = 0;
    if (M->magic != last_mode) {
        last_mode = M->magic;
        global_dirty = 1;
        changed_at = plat_ms();
    }
    engine_apply();
    if (M->start)
        M->start();
    if (M->block) {
        f = plat_lock();
        mode_block = M->block;
        plat_unlock(f);
    }
    if (M->selected)
        M->selected();
    ui.redraw = 1;
}

void app_init(void)
{
    /* from a clean start, whatever ran before (the host simulator's reboot keeps memory) */
    isc_memset(&ui, 0, sizeof ui);
    arp_reset();
    M = 0;
    dirty = global_dirty = 0;
    note_head = note_tail = 0;
    mode_block = 0;
    isc_memset(note_holds, 0, sizeof note_holds);
    pend_voice = pend_fx = pend_pattern = pend_panic = 0;
    pend_program = -1;
    isc_synth_init();
    load_global();
}

/* After the platform is up and the keys are scanned: a piano key held at power-on brings up the chooser; otherwise
 * the last mode used starts (the first, the first time). */
void app_boot(void)
{
    int n, i, pick = 0;
    const md_mode_t *const *all = modes(&n);
    if (plat_keys()) {
        ui.chooser = 1;
        ui.chooser_wait = 1;
        ui.redraw = 1;
        return;
    }
    for (i = 0; i < n; i++)
        if (all[i]->magic == last_mode)
            pick = i;
    start_mode(pick);
}

/* The chooser: once the keys held at power-on are let go, the key under a mode's name starts it. */
static void chooser_input(void)
{
    uint32_t keys = plat_keys();
    int n, i;
    modes(&n);
    if (ui.chooser_wait) {
        if (!keys)
            ui.chooser_wait = 0;
    } else {
        for (i = 0; i < n && i < 8; i++)
            if ((keys >> CHOOSER_KEY[i] & 1u) && !(ui.keys >> CHOOSER_KEY[i] & 1u)) {
                start_mode(i);
                break;
            }
    }
    /* the key that chose stays down as far as the mode is concerned, so it does not play as it starts */
    ui.keys = keys;
    ui.btn = plat_buttons();
}

void app_input_only(void)
{
    if (ui.chooser)
        chooser_input();
    else
        input();
}

int app_expect(const char *what, int *got)
{
    if (isc_streq(what, "chooser"))
        *got = ui.chooser;
    else if (isc_streq(what, "mode"))
        *got = ui.chooser ? -1 : mode_index;
    else if (ui.chooser)
        return 0;
    else if (isc_streq(what, "item"))
        *got = item();
    else if (isc_streq(what, "page"))
        *got = page;
    else if (isc_streq(what, "battery"))
        *got = ui.battery;
    else if (isc_streq(what, "low_battery"))
        *got = ui.low_battery;
    else if (isc_streq(what, "voices"))
        *got = isc_synth_active_voices();
    else if (isc_streq(what, "notes")) {          /* the notes the held keys play */
        int i;
        for (i = 0, *got = 0; i < 128; i++)
            *got += ui.note_refs[i] != 0;
    } else if (isc_streq(what, "arp"))
        *got = arp.running;
    else if (isc_streq(what, "arp_notes"))
        *got = arp.nheld;
    else if (isc_streq(what, "octave"))
        *got = octave;
    else if (isc_streq(what, "dirty"))
        *got = dirty || global_dirty;
    else
        return M->expect ? M->expect(what, got) : 0;
    return 1;
}

#include "screen.c"
#include "modes/iscream.c"
#include "modes/almanac.c"
#include "modes/woof.c"
#include "modes/phase.c"

/* The modes, in the chooser's order. A new mode goes on the end, with a store object of its own (app.h). */
static const md_mode_t *const *modes(int *n)
{
    static const md_mode_t *const all[] = {&MODE_ISCREAM, &MODE_ALMANAC, &MODE_WOOF, &MODE_PHASE};
    *n = (int)(sizeof all / sizeof all[0]);
    return all;
}
