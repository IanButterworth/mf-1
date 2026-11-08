/* Woof on the FM-1: the panel, the dog it edits, the notes each key plays and the screen, as in the desktop app.
 * Part of fm1-base's unity build, after the platform: its drawing and screen reader are in scope. The barks are
 * made by iScream's engine port, which runs only in the audio interrupt, with an arpeggiator beside it for repeats
 * and chases. */
#include "fastmath.h"
#include "isc_libc.h"
#include "wf.h"
#include "wf.c"
#include "synth.h"
#ifdef PLAT_HOST
#include <stdlib.h>
#endif

#define NDOGS WF_DOG_COUNT
#define KEY_VELOCITY 90                  /* the FM-1's keys have no velocity; this is its global default */
#define SCENE_MS 166u                    /* the scene moves at six frames a second */
#define SHOW_MS 10000u                   /* in a dog show, each dog stays on this long */

/* ------------------------------------------------------------------ the model */

enum { P_BARK, P_MOOD, P_PLACE, P_PLAY, NPAGES };
static const char *const PAGE_TITLE[NPAGES] = {"BARK", "MOOD", "PLACE", "PLAY"};
static const char *const STYLES[WF_PLAY_COUNT] = {"Single", "Repeat", "Chorus", "Chase"};
static const char *const PACKS[WF_PACK_COUNT] = {"Pair", "Trio", "Litter"};
static const char *const STEPS[5] = {"1/4", "1/8", "1/8T", "1/16", "1/16T"};

enum { FMT_NUM, FMT_MOOD, FMT_PLACE, FMT_STYLE, FMT_PACK, FMT_STEP };
typedef struct {
    const char *name;
    int field, fmt;
} param_t;

#define STORE_MAGIC 0x31464F57u          /* "WOF1" */
typedef struct {
    uint32_t magic;
    uint8_t version, dog, page, show;
    int8_t octave;
    uint8_t dogs[NDOGS][WF_FIELDS];
} stored_t;

static int32_t table[NDOGS][WF_FIELDS];     /* every dog keeps how it was left */
static int32_t dog[WF_FIELDS];              /* the one being played */
static int page;
static uint8_t show;                        /* a dog show: the next dog comes on every few seconds */
static uint32_t arrived_at, show_at;
static int octave;
static uint8_t dirty;
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
    int8_t battery;
    uint8_t low_battery;
} ui;

static int dog_index(void) { return dog[WF_DOG]; }
static int value(int field) { return dog[field]; }

static void params(int p, param_t out[4])
{
    switch (p) {
    case P_BARK:
        out[0] = (param_t){"Size", WF_SIZE, FMT_NUM};
        out[1] = (param_t){"Gruff", WF_GRUFF, FMT_NUM};
        out[2] = (param_t){"Length", WF_LENGTH, FMT_NUM};
        out[3] = (param_t){"Yelp", WF_YELP, FMT_NUM};
        break;
    case P_MOOD:
        out[0] = (param_t){"Mood", WF_MOOD, FMT_MOOD};
        out[1] = (param_t){"Howl", WF_HOWL, FMT_NUM};
        out[2] = (param_t){"Growl", WF_GROWL, FMT_NUM};
        out[3] = (param_t){"Breath", WF_BREATH, FMT_NUM};
        break;
    case P_PLACE:
        out[0] = (param_t){"Place", WF_PLACE, FMT_PLACE};
        out[1] = (param_t){"Distance", WF_DISTANCE, FMT_NUM};
        out[2] = (param_t){"Echo", WF_ECHO, FMT_NUM};
        out[3] = (param_t){"Space", WF_SPACE, FMT_NUM};
        break;
    default:
        out[0] = (param_t){"Style", WF_PLAY, FMT_STYLE};
        out[1] = (param_t){"Pack", WF_PACK, FMT_PACK};
        out[2] = (param_t){"Tempo", WF_TEMPO, FMT_NUM};
        out[3] = (param_t){"Barks", WF_STEP, FMT_STEP};
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

static void format(int fmt, int v, char *out)
{
    out[0] = 0;
    switch (fmt) {
    case FMT_MOOD: text_cat(out, wf_mood_name(v)); break;
    case FMT_PLACE: text_cat(out, wf_place_name(v)); break;
    case FMT_STYLE: text_cat(out, STYLES[clamp(v, 0, WF_PLAY_COUNT - 1)]); break;
    case FMT_PACK: text_cat(out, PACKS[clamp(v, 0, WF_PACK_COUNT - 1)]); break;
    case FMT_STEP: text_cat(out, STEPS[clamp(v, 0, 4)]); break;
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
    const char *s = getenv("WOOF_BATTERY");   /* the simulator's battery is always full: a test can set another */
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

/* The scene calls these, as the desktop's WoofCore provides them. */
void woof_clamp(int32_t s[WF_FIELDS]) { wf_clamp(s); }

bool woof_dog(int index, WoofDog *out)
{
    const wf_breed_t *b;
    if (index < 0 || index >= NDOGS || !out)
        return false;
    b = wf_breed(index);
    isc_memset(out, 0, sizeof *out);
    out->accent = b->accent;
    out->mono = b->mono != 0;
    return true;
}

/* ------------------------------------------------------------------ the engine's side */

/* The screen's loop hands the engine a new voice, effects and pattern, and the keys' notes; the audio interrupt
 * takes them before its next block. The loop writes with the interrupts held off, so the interrupt never sees half. */
static struct {
    uint8_t voice[155];
    uint8_t mono;
    isc_fx_params_t fx;
    wf_pattern_t pattern;
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
    wf_pattern_t pattern;
    uint32_t f;
    wf_voice(dog, voice);
    wf_fx(dog, &fx);
    wf_pattern(dog, &pattern);
    f = plat_lock();
    isc_memcpy(pend.voice, voice, sizeof voice);
    pend.mono = wf_breed(dog_index())->mono;
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

/* The repeat player: MF-1's arpeggiator, run by the audio interrupt on the engine's 64-sample blocks. Held notes
 * go to it instead of the voices while a dog repeats or a pack chases; the first step is at once, and each note is released at
 * the gate, a block early, so the release never lands after the block the next step fires in. */
static struct {
    wf_pattern_t cfg;
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
        play_note(e & 0x7F, (e & 0x100u) != 0);
        note_tail++;
    }
    midi_in();
    while (i < n) {
        float x[FM_N];
        arp_block();
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

static void key_down(uint32_t k)
{
    int notes[8], n, i, root = 53 + (int)k + 12 * octave;
    if (root < 0 || root > 127)
        return;
    n = wf_key_notes(dog, root, notes);
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
    wf_clamp(dog);
    isc_memcpy(table[dog_index()], dog, sizeof dog);
    engine_apply();
    dirty = 1;
    changed_at = plat_ms();
    ui.redraw = 1;
}

static void set_field(int field, int v)
{
    int lo, hi;
    wf_range(field, &lo, &hi);
    v = clamp(v, lo, hi);
    if (dog[field] == v)
        return;
    /* A new playing style must not leave notes of the old one hanging. */
    if (field == WF_PLAY || field == WF_PACK)
        release_all_keys();
    dog[field] = v;
    touched();
}

/* A new dog trots in. */
static void select_dog(int i)
{
    int next = ((i % NDOGS) + NDOGS) % NDOGS;
    if (next == dog_index())
        return;
    release_all_keys();
    isc_memcpy(table[dog_index()], dog, sizeof dog);
    isc_memcpy(dog, table[next], sizeof dog);
    dog[WF_DOG] = next;
    arrived_at = show_at = plat_ms();
    touched();
}

static void reset_dog(void)
{
    char t[40] = {0};
    release_all_keys();
    wf_default_dog(dog_index(), dog);
    touched();
    text_cat(t, wf_breed(dog_index())->breed);
    text_cat(t, " as bred");
    show_pill(t, 0);
}

static void toggle_show(void)
{
    show = !show;
    show_at = plat_ms();
    dirty = 1;
    changed_at = plat_ms();
    show_pill("Dog show", show ? "On" : "Off");
}

static void set_page(int p)
{
    if (p == page)
        return;
    page = p;
    dirty = 1;
    changed_at = plat_ms();
    ui.redraw = 1;
}

static void turn_knob(int k, int delta)
{
    param_t ps[4], *p;
    char t[16];
    int lo, hi;
    params(page, ps);
    p = &ps[k];
    wf_range(p->field, &lo, &hi);
    set_field(p->field, value(p->field) + delta * (hi - lo + 1 > 12 ? 2 : 1));
    format(p->fmt, value(p->field), t);
    show_pill(p->name, t);
}

/* Steps a short list round, for the buttons that cycle style and pack. */
static void cycle(int field, const char *name, int fmt)
{
    char t[16];
    int lo, hi;
    wf_range(field, &lo, &hi);
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
    dirty = 1;
    changed_at = plat_ms();
}

static int page_for_button(uint32_t b)
{
    switch (b) {
    case B_HOME: return P_BARK;
    case B_FX: return P_MOOD;
    case B_SEL: return P_PLACE;
    case B_ENV: return P_PLAY;
    }
    return -1;
}

static void press(uint32_t b)
{
    int p = page_for_button(b);
    if (p >= 0) {
        set_page(p);
        return;
    }
    switch (b) {
    case B_ARP: select_dog(dog_index() - 1); break;
    case B_SEQ: select_dog(dog_index() + 1); break;
    case B_LFO: cycle(WF_PLAY, "Style", FMT_STYLE); break;
    case B_EDIT: cycle(WF_PACK, "Pack", FMT_PACK); break;
    case B_GLO: toggle_show(); break;
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
        if ((keys & bit) && !(ui.keys & bit))
            key_down(k);
        else if (!(keys & bit) && (ui.keys & bit))
            key_up(k);
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if ((btn & bit) && !(ui.btn & bit) && k != B_OCTDN && k != B_OCTUP)
            press(k);
    }
    /* SAVE is RESET here: held, the dog as bred; pressed, a reminder. */
    if ((btn >> B_SAVE & 1u) && !ui.save_long && now - ui.save_down_at >= 700u) {
        ui.save_long = 1;
        reset_dog();
    }
    if (!(btn >> B_SAVE & 1u) && (ui.btn >> B_SAVE & 1u) && !ui.save_long)
        show_pill("Hold RESET for the dog as bred", 0);
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
        switch (k) {
        case EN_SELECT: set_page(clamp(page + (e > 0 ? 1 : -1), 0, NPAGES - 1)); break;
        case EN_PRESET: select_dog(dog_index() + (e > 0 ? 1 : -1)); break;
        case EN_ALGO:
            set_field(WF_PLACE, value(WF_PLACE) + (e > 0 ? 1 : -1));
            format(FMT_PLACE, value(WF_PLACE), t);
            show_pill("Place", t);
            break;
        default: turn_knob((int)(k - EN_K1), (int)e);
        }
    }
    if (pend_program >= 0) {
        int32_t p = pend_program;
        pend_program = -1;
        if (p < NDOGS)
            select_dog((int)p);
    }
    /* In a dog show the next dog comes on, but never while a key is held. */
    if (show && !keys && now - show_at >= SHOW_MS)
        select_dog(dog_index() + 1);
    /* Keys held across a change of style or octave were released; they play again when pressed again. */
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

static void lights(void)
{
    uint32_t lit = 0, dim = 0, keys = 0, b;
    int k;
    for (b = 0; b < NB; b++)
        if (page_for_button(b) == page)
            lit |= 1u << b;
    if (value(WF_PLAY) != WF_SINGLE)
        dim |= 1u << B_LFO;
    if (show)
        dim |= 1u << B_GLO;
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

/* ------------------------------------------------------------------ keeping the dogs */

static void load(void)
{
    static stored_t st;
    int m, i;
    for (m = 0; m < NDOGS; m++)
        wf_default_dog(m, table[m]);
    page = P_BARK;
    octave = 0;
    show = 0;
    m = 0;
    if (plat_store_load(OBJ_SETTINGS, &st, sizeof st) == (int)sizeof st && st.magic == STORE_MAGIC && st.version == 2) {
        for (m = 0; m < NDOGS; m++) {
            for (i = 0; i < WF_FIELDS; i++)
                table[m][i] = st.dogs[m][i];
            table[m][WF_DOG] = m;
            wf_clamp(table[m]);
        }
        page = clamp(st.page, 0, NPAGES - 1);
        octave = clamp(st.octave, -3, 3);
        show = st.show != 0;
        m = clamp(st.dog, 0, NDOGS - 1);
    }
    isc_memcpy(dog, table[m], sizeof dog);
}

/* A flash write silences the sound for a moment, so it waits until nothing plays. */
static void save_when_quiet(void)
{
    static stored_t st;
    int m, i;
    if (!dirty || plat_ms() - changed_at < 1500u || isc_synth_active_voices() || ui.keys || arp.running)
        return;
    isc_memcpy(table[dog_index()], dog, sizeof dog);
    st.magic = STORE_MAGIC;
    st.version = 2;
    st.dog = (uint8_t)dog_index();
    st.page = (uint8_t)page;
    st.show = show;
    st.octave = (int8_t)octave;
    for (m = 0; m < NDOGS; m++)
        for (i = 0; i < WF_FIELDS; i++)
            st.dogs[m][i] = (uint8_t)table[m][i];
    if (plat_store_save(OBJ_SETTINGS, &st, sizeof st) == 0)
        dirty = 0;
    else
        changed_at = plat_ms();
}

void app_init(void)
{
    isc_synth_init();
    load();
    arrived_at = show_at = plat_ms();
    engine_apply();
}

int app_expect(const char *what, int *got)
{
    if (isc_streq(what, "dog"))
        *got = dog_index();
    else if (isc_streq(what, "page"))
        *got = page;
    else if (isc_streq(what, "place"))
        *got = value(WF_PLACE);
    else if (isc_streq(what, "play"))
        *got = value(WF_PLAY);
    else if (isc_streq(what, "pack"))
        *got = value(WF_PACK);
    else if (isc_streq(what, "show"))
        *got = show;
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
        *got = dirty;
    else
        return 0;
    return 1;
}

#include "screen.c"
