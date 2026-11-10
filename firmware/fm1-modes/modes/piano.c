/* Piano as a mode: a grand piano made from measurements of a real one, played by its own engine in place of the FM
 * voices. The engine and the voicings are the desktop's PianoCore, copied unchanged into firmware/piano/dsp (checked
 * by firmware/piano/tests/run_parity.sh); this file hands it to the core as the instrument, and is how the panel and
 * screen show it. Included by app.c. */
#include "PianoCore.h"
#include "piano.h"

enum { PI_SIGNED = 1, PI_PCT, PI_SPACE, PI_TOUCH };

#define PI_LACQUER 0x0E0D0Cu
#define PI_IVORY 0xF2EBDDu
#define PI_FADED 0x8C857Au
#define PI_BRASS 0xC9A44Cu
#define PI_PLATE 0x3A2F1Eu
#define PI_RATE 44118.f                  /* the FM-1's sample rate */

static pn_piano_t pi_piano PLAT_POOL;
static uint8_t pi_pedal;                 /* PEDAL is down; not saved, as a foot is not */

static const char *const PI_SPACES[3] = {"Room", "Hall", "Plate"};

/* ------------------------------------------------------------------ the instrument, run by the audio interrupt */

static void pi_start(void)
{
    pn_init(&pi_piano, PI_RATE);
    pi_pedal = 0;
}

static void pi_apply(const int32_t *r)
{
    int i;
    for (i = 1; i < PN_FIELDS; i++)
        piano_setting(&pi_piano, i, r[i]);
}

static void pi_on(int note, int vel) { pn_note_on(&pi_piano, note, vel); }
static void pi_off(int note) { pn_note_off(&pi_piano, note); }
static void pi_sustain(int on) { pn_pedal(&pi_piano, on); }
static void pi_all_off(void) { pn_all_off(&pi_piano); }
static void pi_block(float out[64]) { pn_render(&pi_piano, out); }
static int pi_active(void) { return pi_piano.count; }

/* A key's light: while its key is down, or its string still rings with the damper lifted. */
static int pi_sounding(int note)
{
    note &= 127;
    return pi_piano.held[note] || (!pi_piano.damped[note] && pi_piano.level[note] > 1e-8f);
}

static const md_instrument_t PI_INST = {
    .start = pi_start, .apply = pi_apply, .note_on = pi_on, .note_off = pi_off, .sustain = pi_sustain,
    .all_off = pi_all_off, .block = pi_block, .active = pi_active, .sounding = pi_sounding,
};

/* ------------------------------------------------------------------ the mode */

static int pi_count(void) { return piano_count(); }
static void pi_defaults(int i, int32_t *out) { piano_default(i, out); }
static void pi_range(int field, int *lo, int *hi) { piano_range(field, lo, hi); }
static void pi_clamp(int32_t *row_) { piano_clamp(row_); }

/* The FM voices are idle in this mode: they get a silent voice. */
static void pi_voice(const int32_t *s, uint8_t v[155])
{
    (void)s;
    isc_memset(v, 0, 155);
}

static void pi_fx(const int32_t *s, isc_fx_params_t *p)
{
    int cc[24], k;
    piano_fx(s, cc);
    isc_fx_params_default(p);
    for (k = 0; k < 24; k++)
        isc_fx_params_set(p, k, cc[k]);
}

static void pi_pattern(const int32_t *s, md_pattern_t *p)
{
    (void)s;
    isc_memset(p, 0, sizeof *p);
    p->tempo = 120;
}

static int pi_key_notes(const int32_t *s, int note, int left, int out[8])
{
    (void)s, (void)left;
    out[0] = note;
    return 1;
}

static int pi_mono(const int32_t *s) { (void)s; return 0; }
static int pi_releases(int field) { (void)field; return 0; }
static const char *pi_item_name(int i) { return piano_name(i); }
static int pi_velocity(const int32_t *s) { return s[PN_F_TOUCH]; }

static char *pi_end(char *s)
{
    while (*s)
        s++;
    return s;
}

static void pi_format(int fmt, int v, char *out)
{
    static const int16_t TOP[7] = {16, 33, 49, 64, 80, 96, 112};
    static const char *const MARK[8] = {"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"};
    int i;
    switch (fmt) {
    case PI_SIGNED:
        if (v > 50)
            text_cat(out, "+");
        int_text(v - 50, pi_end(out));
        break;
    case PI_PCT:
        int_text(v, out);
        text_cat(out, "%");
        break;
    case PI_SPACE: text_cat(out, PI_SPACES[clamp(v, 0, 2)]); break;
    case PI_TOUCH:
        for (i = 0; i < 7 && v > TOP[i]; i++)
            ;
        int_text(v, out);
        text_cat(out, " ");
        text_cat(out, MARK[i]);
        break;
    }
}

static void pi_press(uint32_t b)
{
    switch (b) {
    case B_SEL: cycle(PN_F_SPACE, "Space", PI_SPACE); break;
    case B_GLO:
        pi_pedal = !pi_pedal;
        md_sustain(pi_pedal);
        show_pill("Pedal", pi_pedal ? "Down" : "Up");
        break;
    }
}

/* The CPU guard: the engine's budget of oscillators follows the audio interrupt's load, so a heavy pedalled chord
 * drops its quietest partials rather than the sound breaking up. The desktop keeps the full budget. */
static uint32_t pi_guard_at;

static void pi_frame(uint32_t now)
{
    uint32_t load;
    int b = pi_piano.budget;
    if (now - pi_guard_at < 100u)
        return;
    pi_guard_at = now;
    load = plat_cpu_pct();
    if (load > 80u && b > 128)
        b -= 32;
    else if (load < 50u && b < PN_MODES)
        b += 16;
    pi_piano.budget = b > PN_MODES ? PN_MODES : b;
}

static uint32_t pi_dim(void) { return 0; }
static uint32_t pi_lit(void) { return pi_pedal ? 1u << B_GLO : 0u; }

/* ------------------------------------------------------------------ the screen */

/* A string's top: long in the bass, short in the treble, as on a grand. */
static int32_t pi_string_top(int i) { return 40 + (5060 * i * i / 7569) / 100 + 40 * i / 87; }

/* The bentside: straight at the bass, curving in towards the treble tail. */
static int32_t pi_rim_top(int32_t x)
{
    int32_t t = (x - 8) * 1000 / 224;           /* 0 .. 1000 along the case */
    int32_t s = t * t / 1000 * (3000 - 2 * t) / 1000;   /* a smooth step */
    return 38 + s * 80 / 1000;
}

static void pi_draw(uint32_t now)
{
    char t[24] = {0};
    int32_t x, i;
    const int32_t bottom = 158;
    fill(0, 0, 240, 240, PI_LACQUER);
    /* the case, the plate and the strings */
    for (x = 8; x < 232; x++) {
        int32_t top = pi_rim_top(x);
        fill(x, top, 1, bottom - top + 4, PI_PLATE);
        fill(x, top, 1, 1, PI_BRASS);
    }
    fill(8, 38, 1, bottom - 34, PI_BRASS);
    fill(231, pi_rim_top(231), 1, bottom - pi_rim_top(231) + 4, PI_BRASS);
    for (i = 0; i < 88; i++) {
        int note = 21 + i;
        float l = pi_piano.level[note];
        int32_t sx = 12 + i * 245 / 100, top = pi_string_top(i), y1, y2;
        uint32_t c = l > 1e-3f ? 0xFFF6DCu : l > 1e-4f ? 0xF3DE9Au : l > 1e-6f ? 0xDDBB66u : 0x6E5A30u;
        int32_t sway = l > 1e-4f ? (int32_t)((now / 40u + (uint32_t)i) & 1u) : 0;
        y1 = top + (bottom - top) / 3;
        y2 = top + 2 * (bottom - top) / 3;
        fill(sx, top, 1, y1 - top, c);
        fill(sx + sway, y1, 1, y2 - y1, c);   /* a ringing string bows at its middle */
        fill(sx, y2, 1, bottom - y2, c);
        if (note < 89) {
            int lifted = pi_pedal || pi_piano.held[note];
            fill(sx - 1, lifted ? 144 : 147, 2, 4, lifted ? 0x4A443Cu : 0x1A1714u);
        }
    }
    fill(8, bottom, 224, 5, 0x000000u);
    if (pi_pedal) {
        fill(182, 41, 48, 15, PI_BRASS);
        text_at(206, 43, &FONT_XS, "PEDAL", PI_LACQUER, 1);
    }
    text_cat(t, "Touch ");
    pi_format(PI_TOUCH, value(PN_F_TOUCH), pi_end(t));
    text_at(14, 42, &FONT_T, t, PI_IVORY, 0);
    /* the title */
    t[0] = 0;
    upper(piano_name(item()), t, 20);
    fill(0, 0, 240, 33, PI_LACQUER);
    text_at(8, 2, &FONT_M, t, PI_IVORY, 0);
    text_at(9, 22, &FONT_T, piano_blurb(item()), PI_FADED, 0);
    fill(0, 32, 240, 1, PI_BRASS);
    draw_battery(207, 9, 1, PI_IVORY);
    draw_tabs(PI_BRASS);
    if (ui.pill[0])
        draw_pill(PI_BRASS);
    else
        draw_chips(PI_BRASS);
}

static int pi_expect(const char *what, int *got)
{
    if (isc_streq(what, "piano"))
        *got = item();
    else if (isc_streq(what, "pedal"))
        *got = pi_pedal;
    else if (isc_streq(what, "partials"))
        *got = pi_piano.count;
    else if (isc_streq(what, "budget"))
        *got = pi_piano.budget;
    else if (isc_streq(what, "touch"))
        *got = value(PN_F_TOUCH);
    else
        return 0;
    return 1;
}

static const md_theme_t PI_THEME = {
    .bg = PI_LACQUER, .ink = PI_IVORY, .faded = PI_FADED, .tab_bg = PI_LACQUER, .tab_ink = PI_IVORY,
    .focus_bg = PI_IVORY, .focus_ink = PI_LACQUER, .chip_bg = PI_LACQUER, .chip_frame = PI_IVORY,
    .chip_ink = PI_IVORY, .bar_off = 0x3A3630u, .pill_bg = PI_IVORY, .pill_ink = PI_LACQUER, .dots = 0, .tab_frame = 1,
};

static const md_mode_t MODE_PIANO = {
    .name = "Piano",
    .reset_words = "as voiced",
    .obj = OBJ_PIANO,
    .magic = 0x314F4950u, /* "PIO1" */
    .version = 1,
    .fields = PN_FIELDS,
    .npages = 2,
    .page_title = {"TONE", "STRING"},
    .params = {{{"Tone", PN_F_TONE, PI_SIGNED}, {"Sustain", PN_F_SUSTAIN, PI_PCT}, {"Room", PN_F_ROOM, FMT_NUM},
                {"Volume", PN_F_VOLUME, FMT_NUM}},
               {{"Unison", PN_F_UNISON, PI_PCT}, {"Knock", PN_F_KNOCK, PI_PCT}, {"Stretch", PN_F_STRETCH, PI_PCT},
                {"Damper", PN_F_DAMPER, PI_PCT}}},
    .algo_field = PN_F_TOUCH,
    .algo_name = "Touch",
    .algo_fmt = PI_TOUCH,
    .theme = &PI_THEME,
    .count = pi_count,
    .defaults = pi_defaults,
    .range = pi_range,
    .clamp = pi_clamp,
    .voice = pi_voice,
    .fx = pi_fx,
    .pattern = pi_pattern,
    .key_notes = pi_key_notes,
    .mono = pi_mono,
    .releases = pi_releases,
    .format = pi_format,
    .item_name = pi_item_name,
    .press = pi_press,
    .dim = pi_dim,
    .lit = pi_lit,
    .frame = pi_frame,
    .draw = pi_draw,
    .idle_ms = 1000,
    .busy_ms = 50,
    .expect = pi_expect,
    .inst = &PI_INST,
    .velocity = pi_velocity,
};
