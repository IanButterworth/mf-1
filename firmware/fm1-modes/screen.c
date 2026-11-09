/* The shared screen: 240x240, drawn in three bands of 80 rows because the canvas holds a third of it, so each fill
 * keeps to the rows of the band. The core draws the mode's scene, the battery, the page tabs, the value chips, the
 * pop-up and the chooser, in the mode's colours; the mode draws what goes over and around its picture. Included at
 * the end of app.c. */

#define BAND_H 80
#define ART_Y 35                         /* the scene's first screen row */
#define SCENE_PX_MAX (120 * 64)          /* the largest scene a mode may draw: every mode shares this one buffer */

static int32_t band_y;                   /* the screen row the canvas's first row is */
static uint32_t scene_px[SCENE_PX_MAX];

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

/* A small round spot. */
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

/* A circle's outline, a pixel wide. With a gap, dotted: one pixel lit, then that many unlit. */
static void ring(int32_t cx, int32_t cy, int32_t r, uint32_t rgb, int gap)
{
    int32_t x = r, y = 0, err = 1 - r, i;
    if (cy + r < band_y || cy - r >= band_y + BAND_H)
        return;
    for (i = 0; x >= y; i++) {
        if (!gap || i % (gap + 1) == 0) {
            fill(cx + x, cy + y, 1, 1, rgb), fill(cx - x, cy + y, 1, 1, rgb);
            fill(cx + x, cy - y, 1, 1, rgb), fill(cx - x, cy - y, 1, 1, rgb);
            fill(cx + y, cy + x, 1, 1, rgb), fill(cx - y, cy + x, 1, 1, rgb);
            fill(cx + y, cy - x, 1, 1, rgb), fill(cx - y, cy - x, 1, 1, rgb);
        }
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* A straight line drawn with a square pen w pixels wide. */
static void segment(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, uint32_t rgb)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy, e2;
    for (;;) {
        fill(x0 - w / 2, y0 - w / 2, w, w, rgb);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy)
            err += dy, x0 += sx;
        if (e2 <= dx)
            err += dx, y0 += sy;
    }
}

/* A point at angle a (radians, clockwise from the right as the screen's rows run down) and radius r from a centre. */
static int32_t polar_x(float cx, float a, float r) { return (int32_t)(cx + fm_cosf(a) * r + 0.5f); }
static int32_t polar_y(float cy, float a, float r) { return (int32_t)(cy + fm_sinf(a) * r + 0.5f); }

/* Part of a circle from angle a0 to a1, drawn with a square pen w pixels wide, its points about a pixel apart. */
static void arc(float cx, float cy, float r, float a0, float a1, int32_t w, uint32_t rgb)
{
    int32_t n = (int32_t)((a1 - a0) * r) + 1, i;
    if (cy + r + (float)w < (float)band_y || cy - r - (float)w >= (float)(band_y + BAND_H))
        return;
    for (i = 0; i <= n; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)n;
        fill(polar_x(cx, a, r) - w / 2, polar_y(cy, a, r) - w / 2, w, w, rgb);
    }
}

static void text_at(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint32_t rgb, int centre)
{
    if (centre)
        x -= text_w(f, s) / 2;
    cv_text(x, y - band_y, f, s, RGB((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu));
}

static void upper(const char *s, char *out, int n)
{
    int i;
    for (i = 0; s[i] && i < n - 1; i++)
        out[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    out[i] = 0;
}

/* The mode's scene, already drawn for this frame, scaled up to the rows of the band. */
static void draw_scene(void)
{
    int32_t r, c, w = M->scene_w, h = M->scene_h, k = M->art_k;
    for (r = 0; r < h; r++) {
        int32_t y = ART_Y + r * k;
        if (y + k <= band_y || y >= band_y + BAND_H)
            continue;
        for (c = 0; c < w;) {
            uint32_t col = scene_px[r * w + c];
            int32_t e = c + 1;
            while (e < w && scene_px[r * w + e] == col)
                e++;
            fill(c * k, y, (e - c) * k, k, col);
            c = e;
        }
    }
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
    fill(0, band_y, 240, BAND_H, 0x101010u);
    draw_battery(84, 78, 3, 0xF4F0E6u);
    text_at(120, 132, &FONT_L, "LOW BATTERY", 0xF4F0E6u, 1);
}

static void draw_tabs(uint32_t accent)
{
    const md_theme_t *t = M->theme;
    int i;
    for (i = 0; i < M->npages && i < 4; i++) {
        int32_t x = 3 + i * 59;
        int focused = page == i;
        fill(x, 167, 56, 29, focused ? t->focus_bg : t->tab_bg);
        if (t->tab_frame)
            frame_rect(x, 167, 56, 29, t->ink);
        if (focused)
            fill(x, 193, 56, 3, accent);
        text_at(x + 28, 173, &FONT_XS, M->page_title[i], focused ? t->focus_ink : t->tab_ink, 1);
    }
}

static void draw_chips(uint32_t accent)
{
    const md_theme_t *t = M->theme;
    int i, b;
    for (i = 0; i < 4; i++) {
        const md_param_t *p = &M->params[page][i];
        int32_t x = 3 + i * 59, y = 200;
        int v = value(p->field), lo, hi, lit;
        char s[16];
        fill(x, y, 56, 37, t->chip_bg);
        frame_rect(x, y, 56, 37, t->chip_frame);
        text_at(x + 28, y + 2, &FONT_T, p->name, t->faded, 1);
        format(p->fmt, v, s);
        text_at(x + 28, y + 13, text_w(&FONT_B, s) > 52 ? &FONT_XS : &FONT_B, s, t->chip_ink, 1);
        /* a stepped bar: blocks, or a row of spots */
        M->range(p->field, &lo, &hi);
        lit = (int)(((v - lo) * 8 + (hi - lo) / 2) / (hi - lo > 0 ? hi - lo : 1));
        for (b = 0; b < 8; b++) {
            if (!t->dots) {
                fill(x + 5 + b * 6, y + 30, 5, 3, b < lit ? accent : t->bar_off);
            } else {
                int32_t cx = x + 8 + b * 6, cy = y + 31;
                uint32_t c = b == lit - 1 ? accent : t->ink;
                if (b < lit) {
                    fill(cx - 1, cy - 2, 3, 5, c);
                    fill(cx - 2, cy - 1, 5, 3, c);
                } else {
                    frame_rect(cx - 1, cy - 1, 3, 3, t->faded);
                }
            }
        }
    }
}

static void draw_pill(uint32_t accent)
{
    const md_theme_t *t = M->theme;
    fill(14, 204, 212, 28, t->pill_bg);
    fill(14, 229, 212, 3, accent);
    text_at(120, 209, &FONT_B, ui.pill, t->pill_ink, 1);
}

/* The chooser: each mode against the white key that starts it. */
static void draw_chooser(void)
{
    int n, i;
    const md_mode_t *const *all = modes(&n);
    fill(0, band_y, 240, BAND_H, 0x15171Cu);
    text_at(120, 10, &FONT_M, "CHOOSE A MODE", 0xF4F0E6u, 1);
    text_at(120, 38, &FONT_T, ui.chooser_wait ? "Let go of the keys" : "Press the key under its name", 0x9AA0AAu, 1);
    for (i = 0; i < n && i < 8; i++) {
        int32_t y = 62 + i * 30;
        char name[24];
        upper(all[i]->name, name, sizeof name);
        fill(20, y, 200, 26, 0x262A33u);
        fill(20, y, 30, 26, 0xF4F0E6u);
        text_at(35, y + 5, &FONT_B, CHOOSER_NOTE[i], 0x15171Cu, 1);
        text_at(60, y + 5, &FONT_B, name, 0xF4F0E6u, 0);
    }
    draw_battery(207, 9, 1, 0xF4F0E6u);
}

static void draw_screen(uint32_t now)
{
    int b;
    if (!ui.chooser && M->scene)
        M->scene(now, scene_px);
    for (b = 0; b < 3; b++) {
        band_y = b * BAND_H;
        cv_begin(240, BAND_H, 0);
        if (ui.low_battery && !ui.chooser) {
            draw_low_battery();
        } else if (ui.chooser) {
            draw_chooser();
        } else {
            if (M->scene) {
                fill(0, band_y, 240, BAND_H, M->theme->bg);
                draw_scene();
            }
            M->draw(now);
        }
        cv_blit(0, (uint32_t)band_y);
    }
}

void app_ui_init(void)
{
    ui.redraw = 1;
    ui.battery = -2;
}

/* The screen moves at the mode's pace (faster while a note sounds, if it likes), and is redrawn at once after any
 * change. */
void app_frame(void)
{
    uint32_t now = plat_ms();
    if (ui.chooser) {
        chooser_input();
    } else {
        input();
        if (M->frame)
            M->frame(now);
        save_when_quiet();
    }
    lights();
    battery();
    if (ui.pill[0] && (int32_t)(now - ui.pill_until) >= 0) {
        ui.pill[0] = 0;
        ui.redraw = 1;
    }
    if (ui.redraw || now - ui.drawn_at >= (ui.chooser ? SCENE_MS : peak_level > 0.0002f ? M->busy_ms : M->idle_ms)) {
        ui.redraw = 0;
        ui.drawn_at = now;
        draw_screen(now);
    }
    ann_tick(now);
}
