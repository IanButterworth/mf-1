/* Woof's 240x240 screen, as the desktop app draws it: the dog's pixel-art scene (the desktop's own scene.c), the four
 * pages and the four values the knobs are on, in ink on white paper. The canvas holds a third of the screen, so the
 * picture is drawn three times, once per band, and each fill keeps to the rows of the band. Included at the end of
 * app.c. */

#define BAND_H 80
#define ART_Y 35                        /* the scene's first screen row */
#define ART_K 2                         /* screen pixels to a scene pixel */
#define INK 0x1A1A1Au
#define PAPER 0xFAF7F0u
#define FADED 0x8A8680u

static int32_t band_y;                  /* the screen row the canvas's first row is */
static uint32_t scene_px[WF_SCENE_W * WF_SCENE_H];

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

/* A small round spot, as on the case. */
static void spot(int32_t cx, int32_t cy, int32_t r, uint32_t rgb)
{
    int32_t dy;
    for (dy = -r; dy <= r; dy++) {
        int32_t dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r)
            dx++;
        fill(cx - dx, cy + dy, dx * 2 + 1, 1, rgb);
    }
}

static void text_at(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint32_t rgb, int centre)
{
    if (centre)
        x -= text_w(f, s) / 2;
    cv_text(x, y - band_y, f, s, RGB((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu));
}

/* The scene, already drawn for this frame, scaled up to the rows of the band. */
static void draw_scene(void)
{
    int32_t row, col;
    for (row = 0; row < WF_SCENE_H; row++) {
        int32_t y = ART_Y + row * ART_K;
        if (y + ART_K <= band_y || y >= band_y + BAND_H)
            continue;
        for (col = 0; col < WF_SCENE_W;) {
            uint32_t c = scene_px[row * WF_SCENE_W + col];
            int32_t e = col + 1;
            while (e < WF_SCENE_W && scene_px[row * WF_SCENE_W + e] == c)
                e++;
            fill(col * ART_K, y, (e - col) * ART_K, ART_K, c);
            col = e;
        }
    }
    fill(0, ART_Y + WF_SCENE_H * ART_K, 240, 1, INK);
}

static void draw_title(void)
{
    const wf_breed_t *b = wf_breed(dog_index());
    char up[20];
    int i;
    for (i = 0; b->breed[i] && i < 19; i++)
        up[i] = b->breed[i] >= 'a' && b->breed[i] <= 'z' ? (char)(b->breed[i] - 32) : b->breed[i];
    up[i] = 0;
    fill(0, 0, 240, 32, PAPER);
    spot(170, 7, 3, INK);
    spot(182, 20, 2, INK);
    spot(160, 24, 1, INK);
    spot(192, 9, 1, INK);
    text_at(8, 2, &FONT_M, up, INK, 0);
    text_at(9, 22, &FONT_T, b->blurb, FADED, 0);
    /* the collar: a band in the dog's colour */
    fill(0, 32, 240, 3, b->accent);
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

/* The place and how a key plays, over the top left of the picture. */
static void draw_badge(void)
{
    char t[48] = {0};
    int32_t w;
    text_cat(t, wf_place_name(value(WF_PLACE)));
    if (value(WF_PLAY) != WF_SINGLE) {
        text_cat(t, " - ");
        text_cat(t, STYLES[value(WF_PLAY)]);
    }
    if (show)
        text_cat(t, " - Show");
    w = text_w(&FONT_T, t) + 10;
    fill(4, 39, w, 14, INK);
    text_at(9, 40, &FONT_T, t, PAPER, 0);
}

/* What the dog says, in a speech bubble with a tail, shaking a little. */
static void draw_bubble(uint32_t step)
{
    const wf_breed_t *b = wf_breed(dog_index());
    char t[12] = {0};
    int32_t j = (int32_t)(step & 1u), x = 150 + j, y = 41 - j, w = 82, h = 24, i;
    text_cat(t, value(WF_GROWL) >= 50 && value(WF_HOWL) < 50 ? "GRRR" : b->word);
    text_cat(t, "!");
    fill(x + 3, y, w - 6, h, PAPER);
    fill(x, y + 3, w, h - 6, PAPER);
    fill(x + 1, y + 1, w - 2, h - 2, PAPER);
    fill(x + 3, y, w - 6, 2, INK);
    fill(x + 3, y + h - 2, w - 6, 2, INK);
    fill(x, y + 3, 2, h - 6, INK);
    fill(x + w - 2, y + 3, 2, h - 6, INK);
    fill(x + 1, y + 1, 2, 2, INK);
    fill(x + w - 3, y + 1, 2, 2, INK);
    fill(x + 1, y + h - 3, 2, 2, INK);
    fill(x + w - 3, y + h - 3, 2, 2, INK);
    /* the tail, down and to the left, towards the dog */
    for (i = 0; i < 9; i++) {
        int32_t tx = x + 14 - i, tw = 10 - i;
        fill(tx, y + h - 2 + i, tw > 2 ? tw : 2, 1, PAPER);
        fill(tx - 1, y + h - 2 + i, 2, 1, INK);
        fill(tx + (tw > 2 ? tw : 2), y + h - 2 + i, 2, 1, INK);
    }
    text_at(x + w / 2, y + 4, &FONT_B, t, INK, 1);
}

static void draw_tabs(void)
{
    uint32_t accent = wf_breed(dog_index())->accent;
    int i;
    for (i = 0; i < NPAGES; i++) {
        int32_t x = 3 + i * 59;
        int focused = page == i;
        fill(x, 167, 56, 29, focused ? INK : PAPER);
        frame_rect(x, 167, 56, 29, INK);
        if (focused)
            fill(x, 193, 56, 3, accent);
        text_at(x + 28, 173, &FONT_XS, PAGE_TITLE[i], focused ? PAPER : INK, 1);
    }
}

static void draw_chips(void)
{
    uint32_t accent = wf_breed(dog_index())->accent;
    param_t ps[4];
    int i, b;
    params(page, ps);
    for (i = 0; i < 4; i++) {
        const param_t *p = &ps[i];
        int32_t x = 3 + i * 59, y = 200;
        int v = value(p->field), lo, hi, lit;
        char t[16];
        fill(x, y, 56, 37, PAPER);
        frame_rect(x, y, 56, 37, INK);
        text_at(x + 28, y + 2, &FONT_T, p->name, FADED, 1);
        format(p->fmt, v, t);
        text_at(x + 28, y + 13, text_w(&FONT_B, t) > 52 ? &FONT_XS : &FONT_B, t, INK, 1);
        /* a row of spots that fill as the value rises */
        wf_range(p->field, &lo, &hi);
        lit = (int)(((v - lo) * 8 + (hi - lo) / 2) / (hi - lo > 0 ? hi - lo : 1));
        for (b = 0; b < 8; b++) {
            int32_t cx = x + 8 + b * 6, cy = y + 31;
            if (b < lit) {
                fill(cx - 1, cy - 2, 3, 5, b == lit - 1 ? accent : INK);
                fill(cx - 2, cy - 1, 5, 3, b == lit - 1 ? accent : INK);
            } else
                frame_rect(cx - 1, cy - 1, 3, 3, FADED);
        }
    }
}

static void draw_pill(void)
{
    fill(14, 204, 212, 28, INK);
    fill(14, 229, 212, 3, wf_breed(dog_index())->accent);
    text_at(120, 209, &FONT_B, ui.pill, PAPER, 1);
}

static void draw_screen(uint32_t step)
{
    int b;
    uint32_t arrived = (plat_ms() - arrived_at) / SCENE_MS;
    int barking = isc_synth_active_voices() > 0;
    woof_scene(dog, (int)step, barking ? 100 : 0, arrived > 1000u ? 1000 : (int)arrived, scene_px);
    for (b = 0; b < 3; b++) {
        band_y = b * BAND_H;
        cv_begin(240, BAND_H, 0);
        if (ui.low_battery) {
            draw_low_battery();
            cv_blit(0, (uint32_t)band_y);
            continue;
        }
        fill(0, band_y, 240, BAND_H, PAPER);
        draw_scene();
        draw_title();
        draw_battery(207, 9, 1, INK);
        draw_badge();
        if (barking && arrived >= 10u)
            draw_bubble(step);
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
