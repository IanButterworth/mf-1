/* Rhymes as a mode: nursery rhymes to learn on the keys, the next key lit. Its songs, game, sounds and pictures are
 * firmware/rhymes/dsp, the desktop's RhymesCore copied unchanged (checked by firmware/rhymes/tests/run_parity.sh);
 * this file is how the panel, the keys' lights and the screen show them. Included by app.c.
 *
 * The game numbers keys as the desktop's panel does, the lower row then the upper; the core numbers them by semitone
 * from F3. The game's notes of its own (a song listened to, the fanfare) start and stop in the audio interrupt, on a
 * clock counted from its blocks; the screen loop takes the lock whenever it touches the game. */
#include "RhymesCore.h"

enum { RM_LEVEL = 1, RM_SPEED, RM_SOUND, RM_LIGHTS };

#define RM_INK 0x2B2B2Bu
#define RM_PAPER 0xFFFDF7u
#define RM_FADED 0x9A948Au
#define RM_GOLD 0xFFC83Du
#define RM_SONGS 16

static rh_game_t rm_game;
static volatile uint32_t rm_ms;          /* the game's clock: the audio interrupt's blocks, in milliseconds */
static uint32_t rm_frac;
static int8_t rm_sent[128];              /* each of the game's own notes, as sent at the octave it started at */
static uint8_t rm_best[RM_SONGS * RH_LEVEL_COUNT];   /* the most stars each song has earned at each level */
static uint32_t rm_scored_at = 1;
static rh_game_t rm_shown;               /* a copy of the game, for the screen */

static int rm_semitone(int key) { return rh_note_of(key) - RH_KEY_BASE; }

/* A mask of the game's keys as the core's. */
static uint32_t rm_keys(uint32_t game)
{
    uint32_t out = 0;
    int k;
    for (k = 0; k < RH_KEYS; k++)
        if (game >> k & 1u)
            out |= 1u << rm_semitone(k);
    return out;
}

static void rm_voice(const int32_t *s, uint8_t v[155]) { rh_voice(s[RH_SOUND], v); }

static void rm_fx(const int32_t *s, isc_fx_params_t *p)
{
    int cc[24], val[24], n, i;
    isc_fx_params_default(p);
    n = rh_fx(s, cc, val);
    for (i = 0; i < n; i++)
        isc_fx_params_set(p, cc[i], val[i]);
}

static void rm_pattern(const int32_t *s, md_pattern_t *p)
{
    (void)s;
    isc_memset(p, 0, sizeof *p);
}

/* A key goes to the game, which says which note it plays: the next note of the song, or none for a wrong key. */
static int rm_key_notes(const int32_t *s, int root, int left, int out[8])
{
    int note = -1, r;
    uint32_t f;
    (void)s, (void)left;
    f = plat_lock();
    r = rh_game_key(&rm_game, rh_key_of(root - 12 * octave), rm_ms, &note);
    plat_unlock(f);
    (void)r;
    ui.redraw = 1;
    if (note < 0 || note + 12 * octave < 0 || note + 12 * octave > 127)
        return 0;
    out[0] = note + 12 * octave;
    return 1;
}

/* In the audio interrupt, before each block: the clock, and the game's own notes. */
static void rm_block(void)
{
    int off[RH_AUTO_MAX], on[RH_AUTO_MAX], offs, ons, i;
    rm_frac += 64000u;
    while (rm_frac >= 44100u) {
        rm_frac -= 44100u;
        rm_ms++;
    }
    rh_game_step(&rm_game, rm_ms, off, &offs, on, &ons);
    for (i = 0; i < offs; i++)
        if (rm_sent[off[i] & 127] >= 0) {
            md_let_go(rm_sent[off[i] & 127]);
            rm_sent[off[i] & 127] = -1;
        }
    for (i = 0; i < ons; i++) {
        int n = on[i] + 12 * octave;
        if (n < 0 || n > 127)
            continue;
        rm_sent[on[i] & 127] = (int8_t)n;
        md_hold(n, 1);
    }
}

static int rm_busy(void)
{
    return rm_game.state == RH_LISTEN || rm_game.state == RH_ENCORE || rm_game.state == RH_DONE || rm_game.auto_count > 0;
}
static int rm_mono(const int32_t *s) { (void)s; return 0; }
static int rm_releases(int field) { (void)field; return 0; }
static const char *rm_item_name(int i) { return rh_song_title(i); }

static void rm_format(int fmt, int v, char *out)
{
    switch (fmt) {
    case RM_LEVEL: text_cat(out, rh_level_name(v)); break;
    case RM_SPEED: int_text(v, out + 0), text_cat(out, "%"); break;
    case RM_SOUND: text_cat(out, rh_sound_name(v)); break;
    case RM_LIGHTS: text_cat(out, rh_lights_name(v)); break;
    }
}

/* The game follows the row: a new song starts it again, other settings it takes as they are. */
static void rm_sync(void)
{
    uint32_t f = plat_lock();
    if (rm_game.tune.song != item() || rm_game.tune.count == 0) {
        int i;
        rh_game_init(&rm_game, row, rm_ms);
        for (i = 0; i < 128; i++)
            rm_sent[i] = -1;
    } else {
        rh_game_settings(&rm_game, row);
    }
    plat_unlock(f);
}

static void rm_restart(void)
{
    uint32_t f = plat_lock();
    rh_game_restart(&rm_game, rm_ms);
    plat_unlock(f);
    show_pill("From the start", 0);
}

static void rm_listen(void)
{
    uint32_t f = plat_lock();
    rh_game_listen(&rm_game, rm_ms);
    plat_unlock(f);
    if (rm_game.state == RH_LISTEN)
        show_pill("Listen...", 0);
    ui.redraw = 1;
}

/* SELECT moves between the song's lines, to practise one. */
static int rm_select(int delta)
{
    char t[16] = {0}, n[8] = {0};
    uint32_t f = plat_lock();
    rh_game_line(&rm_game, delta, rm_ms);
    plat_unlock(f);
    int_text(rm_game.tune.line[rm_game.pos < rm_game.tune.count ? rm_game.pos : 0] + 1, t);
    text_cat(t, " of ");
    int_text(rm_game.tune.lines, n);
    text_cat(t, n);
    show_pill("Line", t);
    return 1;
}

/* FX, SEL and ENV pick a level; LIGHT and SOUND step through theirs; PLAY listens. */
static void rm_press(uint32_t b)
{
    char t[16] = {0};
    switch (b) {
    case B_FX: case B_SEL: case B_ENV:
        set_field(RH_LEVEL, b == B_FX ? RH_MAGIC : b == B_SEL ? RH_GUIDE : RH_SOLO);
        rm_format(RM_LEVEL, value(RH_LEVEL), t);
        show_pill("Level", t);
        break;
    case B_LFO: cycle(RH_LIGHTS, "Lights", RM_LIGHTS); break;
    case B_EDIT: cycle(RH_SOUND, "Sound", RM_SOUND); break;
    case B_PLAY: rm_listen(); break;
    }
}

static uint32_t rm_dim(void) { return 0; }

static uint32_t rm_lit(void)
{
    static const uint8_t LEVEL_BUTTON[RH_LEVEL_COUNT] = {B_FX, B_SEL, B_ENV};
    return 1u << LEVEL_BUTTON[clamp(value(RH_LEVEL), 0, RH_LEVEL_COUNT - 1)] |
           (rm_game.state == RH_LISTEN ? LED_PLAY_GREEN : 0u);
}

static uint32_t rm_key_lights(uint32_t *dim)
{
    uint32_t bright, d, f = plat_lock();
    rh_game_lights(&rm_game, rm_ms, &bright, &d);
    plat_unlock(f);
    *dim = rm_keys(d);
    return rm_keys(bright);
}

/* Every pass of the main loop: the game follows the row, and a finished song keeps its best stars. */
static void rm_frame(uint32_t now)
{
    int s = rm_game.tune.song, lv = rm_game.level;
    (void)now;
    rm_sync();
    if (rm_game.state == RH_DONE && rm_game.done_at != rm_scored_at) {
        rm_scored_at = rm_game.done_at;
        if (s >= 0 && s < RM_SONGS && rm_game.stars > rm_best[s * RH_LEVEL_COUNT + lv]) {
            rm_best[s * RH_LEVEL_COUNT + lv] = (uint8_t)rm_game.stars;
            mark_dirty();
        }
    }
}

static void rm_start(void) { rm_sync(); }

/* A copy of the game, for drawing. */
static void rm_copy(rh_game_t *g, uint32_t *now)
{
    uint32_t f = plat_lock();
    *g = rm_game;
    *now = rm_ms;
    plat_unlock(f);
}

static void rm_scene(uint32_t now, uint32_t *px)
{
    rh_view_t v;
    uint32_t t;
    rm_copy(&rm_shown, &t);
    rh_game_view(&rm_shown, t, isc_synth_active_voices() > 0, &v);
    rh_scene(item(), &v, (int)(now / RH_SCENE_MS), px);
}

/* A five-pointed star, k pixels to each of its 9 by 9. */
static void rm_star(int32_t cx, int32_t cy, int32_t k, uint32_t rgb)
{
    static const uint16_t ROWS[9] = {0x010, 0x038, 0x038, 0x1FF, 0x0FE, 0x07C, 0x0FE, 0x0C6, 0x082};
    int r, c;
    for (r = 0; r < 9; r++)
        for (c = 0; c < 9; c++)
            if (ROWS[r] >> (8 - c) & 1u)
                fill(cx + (c - 4) * k, cy + (r - 4) * k, k, k, rgb);
}

static void rm_badge(const char *s)
{
    int32_t w = text_w(&FONT_B, s) + 20;
    fill(120 - w / 2, 139, w, 22, RM_INK);
    text_at(120, 141, &FONT_B, s, 0xFFFFFFu, 1);
}

static const uint8_t RM_LOWER[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

/* The screen, nothing on it small: the real one is about 3 cm across. The title and the best stars; over the picture
 * a word on what to do; four notes coming up as big coloured beads with their words; and the song's keys, at least
 * eight, the next one in its note's colour. */
static void rm_draw(uint32_t now)
{
    rh_game_t *const gp = &rm_shown;
    static const char *const NAMES[12] = {"C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B"};
    static const char *const HINTS[RH_LEVEL_COUNT] = {"PRESS ANY KEY!", "PRESS THE SHINING KEY!", "PLAY IT YOURSELF!"};
    uint32_t t, accent = rh_song_accent(item()), bright, dim;
    char title[24];
    int cur, i, target = -1, step = (int)(now / RH_SCENE_MS), first, count, playing;
    int32_t kw;
    rm_copy(gp, &t);
    rh_game_lights(gp, t, &bright, &dim);
    cur = rh_game_current(gp, t);
    playing = gp->state == RH_LISTEN || gp->state == RH_ENCORE;
    /* the title and the best stars at this level */
    fill(0, 0, 240, 32, RM_PAPER);
    upper(rh_song_title(item()), title, sizeof title);
    text_at(7, 4, text_w(&FONT_M, title) <= 136 ? &FONT_M : &FONT_B, title, accent, 0);
    for (i = 0; i < 3; i++)
        rm_star(153 + i * 19, 16, 2, i < rm_best[item() * RH_LEVEL_COUNT + value(RH_LEVEL)] ? RM_GOLD : 0xD8D2C6u);
    draw_battery(207, 9, 1, RM_INK);
    fill(0, 32, 240, 3, accent);
    fill(0, 163, 240, 77, RM_PAPER);
    if (gp->state == RH_LISTEN)
        rm_badge("LISTEN");
    else if (gp->state == RH_ENCORE)
        rm_badge("YOUR SONG!");
    else if (gp->state == RH_READY && !ui.pill[0])
        rm_badge(HINTS[clamp(value(RH_LEVEL), 0, RH_LEVEL_COUNT - 1)]);
    if (gp->state == RH_DONE && t - gp->done_at >= 400u) {
        uint32_t since = (t - gp->done_at) / RH_SCENE_MS;
        fill(40, 58, 160, 78, accent);
        fill(43, 61, 154, 72, RM_PAPER);
        text_at(120, 68, &FONT_M, gp->stars >= 3 ? "WOW!" : gp->stars == 2 ? "SUPER!" : "WELL DONE!", accent, 1);
        for (i = 0; i < 3; i++)
            rm_star(92 + i * 28, 112, 2, i < gp->stars && since >= (uint32_t)(7 + i * 3) ? RM_GOLD : 0xD8D2C6u);
    }
    /* four notes coming up, each a bead in its note's colour with its syllable under it */
    if (cur >= 0) {
        for (i = cur; i < gp->tune.count && i < cur + 4; i++) {
            int off = i - cur, note = gp->tune.note[i], big = off == 0;
            int32_t x = big ? 34 : 44 + off * 52, y = 174 - (big && !playing ? (step & 3) == 2 : 0);
            int32_t r = big ? 12 : 10;
            char syl[16];
            if (big)
                spot(x, y, r + 2, RM_INK);
            spot(x, y, r, rh_note_colour(note));
            text_at(x, y - 8, &FONT_B, NAMES[note % 12], 0xFFFFFFu, 1);
            rh_syllable(&gp->tune, i, syl);
            upper(syl, syl, sizeof syl);
            text_at(x, 187, &FONT_B, syl, big ? RM_INK : RM_FADED, 1);
        }
    }
    /* the song's keys, the next one in its note's colour while its light is on */
    if (cur >= 0 && cur < gp->tune.count)
        target = rh_key_of(gp->tune.note[cur]);
    rh_key_window(&gp->tune, &first, &count);
    kw = 240 / count;
    for (i = 0; i < RH_KEYS; i++) {
        int upper_key = i >= 16, below = 0, j;
        int32_t x, w, h;
        uint32_t col = upper_key ? RM_INK : 0xFFFFFFu;
        if (upper_key) {
            int s = rm_semitone(i);
            for (j = 0; j < 16; j++)
                if (RM_LOWER[j] < s)
                    below = j;
            if (below < first || below + 1 >= first + count)
                continue;
            x = (below + 1 - first) * kw - kw / 3, w = kw * 2 / 3, h = 22;
        } else {
            if (i < first || i >= first + count)
                continue;
            x = (i - first) * kw, w = kw, h = 37;
        }
        if (ui.keys >> rm_semitone(i) & 1u)
            col = 0xB8C4CEu;
        else if (i == target && (bright >> i & 1u || playing))
            col = rh_note_colour(gp->tune.note[cur]);
        else if (bright >> i & 1u)
            col = RM_GOLD;
        else if (dim >> i & 1u)
            col = 0xFFE3A0u;
        fill(x, 203, w, h, RM_INK);
        fill(x + 2, upper_key ? 203 : 205, w - 4, h - 2, col);
    }
    /* a turn of a knob shows over the keys for a moment */
    if (ui.pill[0])
        draw_pill(accent);
}

static int rm_expect(const char *what, int *got)
{
    uint32_t bright, dim;
    if (isc_streq(what, "song"))
        *got = item();
    else if (isc_streq(what, "level"))
        *got = value(RH_LEVEL);
    else if (isc_streq(what, "sound"))
        *got = value(RH_SOUND);
    else if (isc_streq(what, "state"))
        *got = rm_game.state;
    else if (isc_streq(what, "pos"))
        *got = rm_game.pos;
    else if (isc_streq(what, "misses"))
        *got = rm_game.misses;
    else if (isc_streq(what, "stars"))
        *got = rm_best[item() * RH_LEVEL_COUNT + value(RH_LEVEL)];
    else if (isc_streq(what, "next_lit")) {   /* the next key, as the core numbers it, lit now or within a blink */
        int k;
        uint32_t t;
        for (k = 0, *got = 0, t = 0; t < 700u && rm_game.pos < rm_game.tune.count; t += 50u) {
            rh_game_lights(&rm_game, rm_ms + t, &bright, &dim);
            if (bright)
                for (k = 0; k < RH_KEYS; k++)
                    if (bright >> k & 1u)
                        *got = rm_semitone(k) + 1;
        }
    } else if (isc_streq(what, "next_key"))
        *got = rm_game.pos < rm_game.tune.count ? rh_note_of(rh_key_of(rm_game.tune.note[rm_game.pos])) - RH_KEY_BASE + 1
                                                 : 0;
    else
        return 0;
    return 1;
}

static const md_theme_t RM_THEME = {
    .bg = RM_PAPER, .ink = RM_INK, .faded = RM_FADED, .tab_bg = RM_PAPER, .tab_ink = RM_INK, .focus_bg = RM_INK,
    .focus_ink = RM_PAPER, .chip_bg = RM_PAPER, .chip_frame = RM_INK, .chip_ink = RM_INK, .bar_off = RM_FADED,
    .pill_bg = RM_INK, .pill_ink = RM_PAPER, .dots = 1, .tab_frame = 1,
};

static const md_mode_t MODE_RHYMES = {
    .name = "Rhymes",
    .reset_words = "as written",
    .obj = OBJ_RHYMES,
    .magic = 0x31594852u, /* "RHY1" */
    .version = 1,
    .fields = RH_FIELDS,
    .npages = 1,
    .page_title = {"SONG"},
    .params = {{{"Speed", RH_SPEED, RM_SPEED}, {"Room", RH_ROOM, FMT_NUM}, {"Sound", RH_SOUND, RM_SOUND},
                {"Lights", RH_LIGHTS, RM_LIGHTS}}},
    .algo_field = RH_LEVEL,
    .algo_name = "Level",
    .algo_fmt = RM_LEVEL,
    .theme = &RM_THEME,
    .more = rm_best,
    .more_len = sizeof rm_best,
    .count = rh_song_count,
    .defaults = rh_default,
    .range = rh_range,
    .clamp = rh_clamp,
    .voice = rm_voice,
    .fx = rm_fx,
    .pattern = rm_pattern,
    .key_notes = rm_key_notes,
    .mono = rm_mono,
    .releases = rm_releases,
    .format = rm_format,
    .item_name = rm_item_name,
    .press = rm_press,
    .dim = rm_dim,
    .frame = rm_frame,
    .start = rm_start,
    .scene = rm_scene,
    .scene_w = RH_SCENE_W,
    .scene_h = RH_SCENE_H,
    .art_k = 2,
    .draw = rm_draw,
    .idle_ms = RH_SCENE_MS,
    .busy_ms = RH_SCENE_MS,
    .expect = rm_expect,
    .block = rm_block,
    .busy = rm_busy,
    .lit = rm_lit,
    .save_press = rm_restart,
    .key_lights = rm_key_lights,
    .select = rm_select,
};
