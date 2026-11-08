/* Woof as a mode: a dog show, every key a bark. Its dogs, barks and effects are firmware/woof/dsp's port of the
 * desktop's WoofCore (checked by firmware/woof/tests/run_parity.sh); this file is how the panel and screen show them,
 * and the dog show. Included by app.c. */
#include "wf.h"

enum { WM_MOOD = 1, WM_PLACE, WM_STYLE, WM_PACK, WM_STEP };

static const char *const WM_STYLES[WF_PLAY_COUNT] = {"Single", "Repeat", "Chorus", "Chase"};
static const char *const WM_PACKS[WF_PACK_COUNT] = {"Pair", "Trio", "Litter"};
static const char *const WM_STEPS[5] = {"1/4", "1/8", "1/8T", "1/16", "1/16T"};

#define WM_INK 0x1A1A1Au
#define WM_PAPER 0xFAF7F0u
#define WM_FADED 0x8A8680u
#define WM_SHOW_MS 10000u                /* in a dog show, each dog stays on this long */

/* The dog show is on (saved with the dogs), and when the dog came on, so it trots in. */
static uint8_t wm_state[1];
static uint32_t wm_arrived_at, wm_show_at;
#define wm_show wm_state[0]

/* The scene calls these, as the desktop's WoofCore provides them. */
void woof_clamp(int32_t s[WF_FIELDS]) { wf_clamp(s); }

bool woof_dog(int index, WoofDog *out)
{
    const wf_breed_t *b;
    if (index < 0 || index >= wf_dog_count() || !out)
        return false;
    b = wf_breed(index);
    isc_memset(out, 0, sizeof *out);
    out->accent = b->accent;
    out->mono = b->mono != 0;
    return true;
}

static void wm_pattern(const int32_t *s, md_pattern_t *p)
{
    wf_pattern_t a;
    wf_pattern(s, &a);
    p->on = a.on, p->mode = a.mode, p->octaves = a.octaves, p->rate = a.rate, p->gate = a.gate, p->tempo = a.tempo;
}

static int wm_key_notes(const int32_t *s, int note, int left, int out[8])
{
    (void)left;
    return wf_key_notes(s, note, out);
}

static int wm_mono(const int32_t *s) { return wf_breed(s[WF_DOG])->mono; }
static int wm_releases(int field) { return field == WF_PLAY || field == WF_PACK; }
static const char *wm_item_name(int i) { return wf_breed(i)->breed; }

static void wm_format(int fmt, int v, char *out)
{
    switch (fmt) {
    case WM_MOOD: text_cat(out, wf_mood_name(v)); break;
    case WM_PLACE: text_cat(out, wf_place_name(v)); break;
    case WM_STYLE: text_cat(out, WM_STYLES[clamp(v, 0, WF_PLAY_COUNT - 1)]); break;
    case WM_PACK: text_cat(out, WM_PACKS[clamp(v, 0, WF_PACK_COUNT - 1)]); break;
    case WM_STEP: text_cat(out, WM_STEPS[clamp(v, 0, 4)]); break;
    }
}

static void wm_press(uint32_t b)
{
    switch (b) {
    case B_LFO: cycle(WF_PLAY, "Style", WM_STYLE); break;
    case B_EDIT: cycle(WF_PACK, "Pack", WM_PACK); break;
    case B_GLO:
        wm_show = !wm_show;
        wm_show_at = plat_ms();
        mark_dirty();
        show_pill("Dog show", wm_show ? "On" : "Off");
        break;
    }
}

static uint32_t wm_dim(void)
{
    return (value(WF_PLAY) != WF_SINGLE ? 1u << B_LFO : 0u) | (wm_show ? 1u << B_GLO : 0u);
}

/* A new dog trots in. */
static void wm_selected(void) { wm_arrived_at = wm_show_at = plat_ms(); }

/* In a dog show the next dog comes on, but never while a key is held. */
static void wm_frame(uint32_t now)
{
    if (wm_show && !ui.keys && now - wm_show_at >= WM_SHOW_MS)
        select_item(item() + 1);
}

static uint32_t wm_arrived(void)
{
    uint32_t a = (plat_ms() - wm_arrived_at) / SCENE_MS;
    return a > 1000u ? 1000u : a;
}

static void wm_scene(uint32_t step, uint32_t *px)
{
    woof_scene(row, (int)step, isc_synth_active_voices() > 0 ? 100 : 0, (int)wm_arrived(), px);
}

/* What the dog says, in a speech bubble with a tail, shaking a little. */
static void wm_bubble(uint32_t step)
{
    char t[12] = {0};
    int32_t j = (int32_t)(step & 1u), x = 150 + j, y = 41 - j, w = 82, h = 24, i;
    text_cat(t, value(WF_GROWL) >= 50 && value(WF_HOWL) < 50 ? "GRRR" : wf_breed(item())->word);
    text_cat(t, "!");
    fill(x + 3, y, w - 6, h, WM_PAPER);
    fill(x, y + 3, w, h - 6, WM_PAPER);
    fill(x + 1, y + 1, w - 2, h - 2, WM_PAPER);
    fill(x + 3, y, w - 6, 2, WM_INK);
    fill(x + 3, y + h - 2, w - 6, 2, WM_INK);
    fill(x, y + 3, 2, h - 6, WM_INK);
    fill(x + w - 2, y + 3, 2, h - 6, WM_INK);
    fill(x + 1, y + 1, 2, 2, WM_INK);
    fill(x + w - 3, y + 1, 2, 2, WM_INK);
    fill(x + 1, y + h - 3, 2, 2, WM_INK);
    fill(x + w - 3, y + h - 3, 2, 2, WM_INK);
    for (i = 0; i < 9; i++) {
        int32_t tx = x + 14 - i, tw = 10 - i > 2 ? 10 - i : 2;
        fill(tx, y + h - 2 + i, tw, 1, WM_PAPER);
        fill(tx - 1, y + h - 2 + i, 2, 1, WM_INK);
        fill(tx + tw, y + h - 2 + i, 2, 1, WM_INK);
    }
    text_at(x + w / 2, y + 4, &FONT_B, t, WM_INK, 1);
}

static void wm_draw(uint32_t step)
{
    const wf_breed_t *b = wf_breed(item());
    char t[48] = {0};
    fill(0, ART_Y + WF_SCENE_H * 2, 240, 1, WM_INK);
    upper(b->breed, t, 20);
    fill(0, 0, 240, 32, WM_PAPER);
    spot(170, 7, 3, WM_INK);
    spot(182, 20, 2, WM_INK);
    spot(160, 24, 1, WM_INK);
    spot(192, 9, 1, WM_INK);
    text_at(8, 2, &FONT_M, t, WM_INK, 0);
    text_at(9, 22, &FONT_T, b->blurb, WM_FADED, 0);
    fill(0, 32, 240, 3, b->accent);    /* the collar: a band in the dog's colour */
    draw_battery(207, 9, 1, WM_INK);
    /* the place and how a key plays, over the top left of the picture */
    t[0] = 0;
    text_cat(t, wf_place_name(value(WF_PLACE)));
    if (value(WF_PLAY) != WF_SINGLE) {
        text_cat(t, " - ");
        text_cat(t, WM_STYLES[value(WF_PLAY)]);
    }
    if (wm_show)
        text_cat(t, " - Show");
    fill(4, 39, text_w(&FONT_T, t) + 10, 14, WM_INK);
    text_at(9, 40, &FONT_T, t, WM_PAPER, 0);
    if (isc_synth_active_voices() > 0 && wm_arrived() >= 10u)
        wm_bubble(step);
    draw_tabs(b->accent);
    if (ui.pill[0])
        draw_pill(b->accent);
    else
        draw_chips(b->accent);
}

static int wm_expect(const char *what, int *got)
{
    if (isc_streq(what, "dog"))
        *got = item();
    else if (isc_streq(what, "place"))
        *got = value(WF_PLACE);
    else if (isc_streq(what, "play"))
        *got = value(WF_PLAY);
    else if (isc_streq(what, "pack"))
        *got = value(WF_PACK);
    else if (isc_streq(what, "show"))
        *got = wm_show;
    else
        return 0;
    return 1;
}

static const md_theme_t WM_THEME = {
    .bg = WM_PAPER, .ink = WM_INK, .faded = WM_FADED, .tab_bg = WM_PAPER, .tab_ink = WM_INK, .focus_bg = WM_INK,
    .focus_ink = WM_PAPER, .chip_bg = WM_PAPER, .chip_frame = WM_INK, .chip_ink = WM_INK, .bar_off = WM_FADED,
    .pill_bg = WM_INK, .pill_ink = WM_PAPER, .dots = 1, .tab_frame = 1,
};

static const md_mode_t MODE_WOOF = {
    .name = "Woof",
    .reset_words = "as bred",
    .obj = OBJ_WOOF,
    .magic = 0x31464F57u, /* "WOF1" */
    .version = 2,
    .fields = WF_FIELDS,
    .page_title = {"BARK", "MOOD", "PLACE", "PLAY"},
    .params = {{{"Size", WF_SIZE, FMT_NUM}, {"Gruff", WF_GRUFF, FMT_NUM}, {"Length", WF_LENGTH, FMT_NUM},
                {"Yelp", WF_YELP, FMT_NUM}},
               {{"Mood", WF_MOOD, WM_MOOD}, {"Howl", WF_HOWL, FMT_NUM}, {"Growl", WF_GROWL, FMT_NUM},
                {"Breath", WF_BREATH, FMT_NUM}},
               {{"Place", WF_PLACE, WM_PLACE}, {"Distance", WF_DISTANCE, FMT_NUM}, {"Echo", WF_ECHO, FMT_NUM},
                {"Space", WF_SPACE, FMT_NUM}},
               {{"Style", WF_PLAY, WM_STYLE}, {"Pack", WF_PACK, WM_PACK}, {"Tempo", WF_TEMPO, FMT_NUM},
                {"Barks", WF_STEP, WM_STEP}}},
    .algo_field = WF_PLACE,
    .algo_name = "Place",
    .algo_fmt = WM_PLACE,
    .theme = &WM_THEME,
    .extra = wm_state,
    .extra_len = sizeof wm_state,
    .count = wf_dog_count,
    .defaults = wf_default_dog,
    .range = wf_range,
    .clamp = wf_clamp,
    .voice = wf_voice,
    .fx = wf_fx,
    .pattern = wm_pattern,
    .key_notes = wm_key_notes,
    .mono = wm_mono,
    .releases = wm_releases,
    .format = wm_format,
    .item_name = wm_item_name,
    .press = wm_press,
    .dim = wm_dim,
    .frame = wm_frame,
    .selected = wm_selected,
    .scene = wm_scene,
    .scene_w = WF_SCENE_W,
    .scene_h = WF_SCENE_H,
    .art_k = 2,
    .draw = wm_draw,
    .expect = wm_expect,
};
