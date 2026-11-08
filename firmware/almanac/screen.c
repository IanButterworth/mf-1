/* Almanac's 240x240 screen, as the desktop app draws it: the month's pixel-art scene (the desktop's own scene.c),
 * the four pages and the four values the knobs are on. The canvas holds a third of the screen, so the picture is
 * drawn three times, once per band, and each fill keeps to the rows of the band. Included at the end of app.c. */

#define BAND_H 80
#define ART_Y 35                        /* the scene's first screen row */
#define ART_K 3                         /* screen pixels to a scene pixel */
#define INK 0x1C2233u
#define PAPER 0xEFE8D8u
#define FADED 0x8C93A8u
#define CHIP_BG 0x2A3042u
#define TAB_BG 0x2A3042u

static int32_t band_y;                  /* the screen row the canvas's first row is */
static uint32_t scene_px[AL_SCENE_W * AL_SCENE_H];

static uint16_t px565(uint32_t rgb) { return swap16(RGB((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu)); }

/* A filled rectangle in screen coordinates, cut to the band. */
static void fill(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb)
{
    int32_t y0 = y < band_y ? band_y : y, y1 = y + h > band_y + BAND_H ? band_y + BAND_H : y + h, r, c;
    int32_t x0 = x < 0 ? 0 : x, x1 = x + w > 240 ? 240 : x + w;
    uint16_t v = px565(rgb);
    for (r = y0; r < y1; r++) {
        uint16_t *p = &cv_px[(uint32_t)(r - band_y) * 240u];
        for (c = x0; c < x1; c++)
            p[c] = v;
    }
}

static void frame_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb)
{
    fill(x, y, w, 1, rgb);
    fill(x, y + h - 1, w, 1, rgb);
    fill(x, y, 1, h, rgb);
    fill(x + w - 1, y, 1, h, rgb);
}

static void text_at(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint32_t rgb, int centre)
{
    if (centre)
        x -= text_w(f, s) / 2;
    cv_text(x, y - band_y, f, s, RGB((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu));
}

static uint32_t lighter(uint32_t c)
{
    uint32_t out = 0;
    int s;
    for (s = 0; s <= 16; s += 8)
        out |= ((((c >> s) & 0xFFu) * 2u + 0xFFu * 3u) / 5u) << s;
    return out;
}

/* The scene, already drawn for this frame, scaled up to the rows of the band. */
static void draw_scene(void)
{
    int32_t row, col;
    for (row = 0; row < AL_SCENE_H; row++) {
        int32_t y = ART_Y + row * ART_K;
        if (y + ART_K <= band_y || y >= band_y + BAND_H)
            continue;
        for (col = 0; col < AL_SCENE_W;) {
            uint32_t c = scene_px[row * AL_SCENE_W + col];
            int32_t e = col + 1;
            while (e < AL_SCENE_W && scene_px[row * AL_SCENE_W + e] == c)
                e++;
            fill(col * ART_K, y, (e - col) * ART_K, ART_K, c);
            col = e;
        }
    }
}

static void draw_title(void)
{
    const alm_month_t *m = alm_month(month());
    char up[16], sub[72] = {0};
    int i;
    for (i = 0; m->month[i] && i < 15; i++)
        up[i] = m->month[i] >= 'a' && m->month[i] <= 'z' ? (char)(m->month[i] - 32) : m->month[i];
    up[i] = 0;
    fill(0, 0, 240, 34, INK);
    text_at(8, 2, &FONT_M, up, PAPER, 0);
    text_cat(sub, m->title);
    text_cat(sub, " - ");
    text_cat(sub, m->blurb);
    text_at(9, 22, &FONT_T, sub, lighter(m->accent), 0);
    fill(0, 34, 240, 1, m->accent);
}

/* The FM-1's battery icon: up to three bars, and a bolt while USB powers it. Drawn in square pixels. */
static void draw_battery(int32_t x, int32_t y, int32_t k, uint32_t rgb)
{
    int i;
    if (ui.battery < 0)
        return;
    frame_rect(x, y, 24 * k, 13 * k, rgb);
    if (k > 1)
        frame_rect(x + 1, y + 1, 24 * k - 2, 13 * k - 2, rgb);
    fill(x + 24 * k, y + 4 * k, 2 * k, 5 * k, rgb);
    if (ui.battery == 4) {
        static const uint8_t BOLT[9][2] = {{13, 2}, {12, 3}, {11, 4}, {10, 5}, {9, 6}, {12, 6}, {11, 7}, {10, 8}, {9, 9}};
        for (i = 0; i < 9; i++)
            fill(x + BOLT[i][0] * k, y + BOLT[i][1] * k, 3 * k, k, rgb);
        return;
    }
    for (i = 0; i < ui.battery && i < 3; i++)
        fill(x + (3 + 6 * i) * k, y + 3 * k, 5 * k, 7 * k, rgb);
}

static void draw_low_battery(void)
{
    fill(0, band_y, 240, BAND_H, INK);
    draw_battery(84, 78, 3, PAPER);
    text_at(120, 132, &FONT_L, "LOW BATTERY", PAPER, 1);
}

/* The sky, the place and how a key plays, over the top left of the picture. */
static void draw_badge(void)
{
    char t[48] = {0};
    int32_t w;
    text_cat(t, alm_sky(value(AL_SKY))->name);
    text_cat(t, " - ");
    text_cat(t, alm_place_name(value(AL_PLACE)));
    if (value(AL_PLAY) != AL_SINGLE) {
        text_cat(t, " - ");
        text_cat(t, STYLES[value(AL_PLAY)]);
    }
    w = text_w(&FONT_T, t) + 10;
    fill(4, 39, w, 14, INK);
    text_at(9, 40, &FONT_T, t, PAPER, 0);
}

static void draw_tabs(void)
{
    uint32_t accent = alm_month(month())->accent;
    int i;
    for (i = 0; i < NPAGES; i++) {
        int32_t x = 3 + i * 59;
        int focused = page == i;
        fill(x, 167, 56, 29, focused ? PAPER : TAB_BG);
        if (focused)
            fill(x, 193, 56, 3, accent);
        text_at(x + 28, 173, &FONT_XS, PAGE_TITLE[i], focused ? INK : FADED, 1);
    }
}

static void draw_chips(void)
{
    uint32_t accent = alm_month(month())->accent;
    param_t ps[4];
    int i, b;
    params(page, ps);
    for (i = 0; i < 4; i++) {
        const param_t *p = &ps[i];
        int32_t x = 3 + i * 59, y = 200;
        int v = value(p->field), lo, hi, lit;
        char t[16];
        fill(x, y, 56, 37, CHIP_BG);
        frame_rect(x, y, 56, 37, 0x434A5Eu);
        text_at(x + 28, y + 2, &FONT_T, p->name, FADED, 1);
        format(p->fmt, v, t);
        text_at(x + 28, y + 13, text_w(&FONT_B, t) > 52 ? &FONT_XS : &FONT_B, t, PAPER, 1);
        /* A stepped bar, in blocks, as the scene is drawn. */
        alm_range(p->field, &lo, &hi);
        lit = (int)(((v - lo) * 8 + (hi - lo) / 2) / (hi - lo > 0 ? hi - lo : 1));
        for (b = 0; b < 8; b++)
            fill(x + 5 + b * 6, y + 30, 5, 3, b < lit ? accent : 0x3A4154u);
    }
}

static void draw_pill(void)
{
    fill(14, 204, 212, 28, PAPER);
    fill(14, 229, 212, 3, alm_month(month())->accent);
    text_at(120, 209, &FONT_B, ui.pill, INK, 1);
}

static void draw_screen(uint32_t step)
{
    int b, activity = isc_synth_active_voices() * 25;
    almanac_scene(day, (int)step, activity > 100 ? 100 : activity, scene_px);
    for (b = 0; b < 3; b++) {
        band_y = b * BAND_H;
        cv_begin(240, BAND_H, 0);
        if (ui.low_battery) {
            draw_low_battery();
            cv_blit(0, (uint32_t)band_y);
            continue;
        }
        fill(0, band_y, 240, BAND_H, INK);
        draw_scene();
        draw_title();
        draw_battery(207, 9, 1, PAPER);
        draw_badge();
        draw_tabs();
        if (ui.pill[0])
            draw_pill();
        else
            draw_chips();
        cv_blit(0, (uint32_t)band_y);
    }
}

void app_ui_init(void)
{
    ui.redraw = 1;
    ui.battery = -2;
}

/* The scene moves at a few frames a second, and the screen is redrawn at once after any change. */
void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    lights();
    save_when_quiet();
    battery();
    if (ui.pill[0] && (int32_t)(now - ui.pill_until) >= 0) {
        ui.pill[0] = 0;
        ui.redraw = 1;
    }
    if (ui.redraw || now - ui.drawn_at >= SCENE_MS) {
        ui.redraw = 0;
        ui.drawn_at = now;
        draw_screen(now / SCENE_MS);
    }
    ann_tick(now);
}
