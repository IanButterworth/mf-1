/* Almanac as a mode: the year as twelve sounds. Its months, voices and effects are firmware/almanac/dsp's port of the
 * desktop's AlmanacCore (checked by firmware/almanac/tests/run_parity.sh); this file is how the panel and screen show
 * them. Included by app.c. */
#include "alm.h"

enum { AM_MOOD = 1, AM_SKY, AM_PLACE, AM_STYLE, AM_VOICING, AM_STEP };

static const char *const AM_STYLES[AL_PLAY_COUNT] = {"Single", "Chord", "Split", "Pattern"};
static const char *const AM_VOICINGS[AL_VOICING_COUNT] = {"Close", "Open", "Wide"};
static const char *const AM_STEPS[5] = {"1/4", "1/8", "1/8T", "1/16", "1/16T"};

#define AM_INK 0x1C2233u
#define AM_PAPER 0xEFE8D8u
#define AM_FADED 0x8C93A8u

/* The scene calls these, as the desktop's AlmanacCore provides them. */
void almanac_clamp(int32_t s[AL_FIELDS]) { alm_clamp(s); }

bool almanac_month(int index, AlmanacMonth *out)
{
    const alm_month_t *m;
    if (index < 0 || index >= alm_month_count() || !out)
        return false;
    m = alm_month(index);
    isc_memset(out, 0, sizeof *out);
    out->skyTop = m->sky_top;
    out->skyBottom = m->sky_bottom;
    out->ground = m->ground;
    out->far = m->far;
    out->accent = m->accent;
    out->season = m->season;
    out->mono = m->mono != 0;
    return true;
}

static void am_pattern(const int32_t *s, md_pattern_t *p)
{
    alm_pattern_t a;
    alm_pattern(s, &a);
    p->on = a.on, p->mode = a.mode, p->octaves = a.octaves, p->rate = a.rate, p->gate = a.gate, p->tempo = a.tempo;
}

static int am_mono(const int32_t *s) { return alm_month(s[AL_MONTH])->mono; }
static int am_releases(int field) { return field == AL_PLAY || field == AL_VOICING; }
static const char *am_item_name(int i) { return alm_month(i)->month; }

static void am_format(int fmt, int v, char *out)
{
    switch (fmt) {
    case AM_MOOD: text_cat(out, alm_mood_name(v)); break;
    case AM_SKY: text_cat(out, alm_sky(v)->name); break;
    case AM_PLACE: text_cat(out, alm_place_name(v)); break;
    case AM_STYLE: text_cat(out, AM_STYLES[clamp(v, 0, AL_PLAY_COUNT - 1)]); break;
    case AM_VOICING: text_cat(out, AM_VOICINGS[clamp(v, 0, AL_VOICING_COUNT - 1)]); break;
    case AM_STEP: text_cat(out, AM_STEPS[clamp(v, 0, 4)]); break;
    }
}

static void am_press(uint32_t b)
{
    switch (b) {
    case B_LFO: cycle(AL_PLAY, "Style", AM_STYLE); break;
    case B_EDIT: cycle(AL_VOICING, "Voicing", AM_VOICING); break;
    case B_GLO: cycle(AL_SKY, "Sky", AM_SKY); break;
    }
}

static uint32_t am_dim(void)
{
    return (value(AL_PLAY) != AL_SINGLE ? 1u << B_LFO : 0u) | (value(AL_SKY) != AL_CLEAR ? 1u << B_GLO : 0u);
}

static void am_scene(uint32_t now, uint32_t *px)
{
    uint32_t step = now / SCENE_MS;
    int activity = isc_synth_active_voices() * 25;
    almanac_scene(row, (int)step, activity > 100 ? 100 : activity, px);
}

static uint32_t am_lighter(uint32_t c)
{
    uint32_t out = 0;
    int s;
    for (s = 0; s <= 16; s += 8)
        out |= ((((c >> s) & 0xFFu) * 2u + 0xFFu * 3u) / 5u) << s;
    return out;
}

static void am_draw(uint32_t now)
{
    uint32_t step = now / SCENE_MS;
    const alm_month_t *m = alm_month(item());
    char up[16], t[72] = {0};
    (void)step;
    upper(m->month, up, sizeof up);
    fill(0, 0, 240, 34, AM_INK);
    text_at(8, 2, &FONT_M, up, AM_PAPER, 0);
    text_cat(t, m->title);
    text_cat(t, " - ");
    text_cat(t, m->blurb);
    text_at(9, 22, &FONT_T, t, am_lighter(m->accent), 0);
    fill(0, 34, 240, 1, m->accent);
    draw_battery(207, 9, 1, AM_PAPER);
    /* the sky, the place and how a key plays, over the top left of the picture */
    t[0] = 0;
    text_cat(t, alm_sky(value(AL_SKY))->name);
    text_cat(t, " - ");
    text_cat(t, alm_place_name(value(AL_PLACE)));
    if (value(AL_PLAY) != AL_SINGLE) {
        text_cat(t, " - ");
        text_cat(t, AM_STYLES[value(AL_PLAY)]);
    }
    fill(4, 39, text_w(&FONT_T, t) + 10, 14, AM_INK);
    text_at(9, 40, &FONT_T, t, AM_PAPER, 0);
    draw_tabs(m->accent);
    if (ui.pill[0])
        draw_pill(m->accent);
    else
        draw_chips(m->accent);
}

static int am_expect(const char *what, int *got)
{
    if (isc_streq(what, "month"))
        *got = item();
    else if (isc_streq(what, "sky"))
        *got = value(AL_SKY);
    else if (isc_streq(what, "play"))
        *got = value(AL_PLAY);
    else if (isc_streq(what, "mood"))
        *got = value(AL_MOOD);
    else
        return 0;
    return 1;
}

static const md_theme_t AM_THEME = {
    .bg = AM_INK, .ink = AM_INK, .faded = AM_FADED, .tab_bg = 0x2A3042u, .tab_ink = AM_FADED, .focus_bg = AM_PAPER,
    .focus_ink = AM_INK, .chip_bg = 0x2A3042u, .chip_frame = 0x434A5Eu, .chip_ink = AM_PAPER, .bar_off = 0x3A4154u,
    .pill_bg = AM_PAPER, .pill_ink = AM_INK, .dots = 0, .tab_frame = 0,
};

static const md_mode_t MODE_ALMANAC = {
    .name = "Almanac",
    .reset_words = "as composed",
    .obj = OBJ_ALMANAC,
    .magic = 0x314D4C41u, /* "ALM1" */
    .version = 1,
    .fields = AL_FIELDS,
    .npages = 4,
    .page_title = {"MOOD", "WEATHER", "LAND", "PLAY"},
    .params = {{{"Mood", AL_MOOD, AM_MOOD}, {"Warmth", AL_WARMTH, FMT_NUM}, {"Linger", AL_LINGER, FMT_NUM},
                {"Shimmer", AL_SHIMMER, FMT_NUM}},
               {{"Sky", AL_SKY, AM_SKY}, {"Weather", AL_WEATHER, FMT_NUM}, {"Elements", AL_ELEMENTS, FMT_NUM},
                {"Gusts", AL_GUSTS, FMT_NUM}},
               {{"Place", AL_PLACE, AM_PLACE}, {"Distance", AL_DISTANCE, FMT_NUM}, {"Echo", AL_ECHO, FMT_NUM},
                {"Space", AL_SPACE, FMT_NUM}},
               {{"Style", AL_PLAY, AM_STYLE}, {"Voicing", AL_VOICING, AM_VOICING}, {"Tempo", AL_TEMPO, FMT_NUM},
                {"Notes", AL_STEP, AM_STEP}}},
    .algo_field = AL_SKY,
    .algo_name = "Sky",
    .algo_fmt = AM_SKY,
    .theme = &AM_THEME,
    .count = alm_month_count,
    .defaults = alm_default_day,
    .range = alm_range,
    .clamp = alm_clamp,
    .voice = alm_voice,
    .fx = alm_fx,
    .pattern = am_pattern,
    .key_notes = alm_key_notes,
    .mono = am_mono,
    .releases = am_releases,
    .format = am_format,
    .item_name = am_item_name,
    .press = am_press,
    .dim = am_dim,
    .scene = am_scene,
    .scene_w = AL_SCENE_W,
    .scene_h = AL_SCENE_H,
    .art_k = 3,
    .draw = am_draw,
    .idle_ms = SCENE_MS,
    .busy_ms = SCENE_MS,
    .expect = am_expect,
};
