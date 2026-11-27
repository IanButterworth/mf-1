/* Rhymes' pictures: one small pixel-art scene for each song, which the song's notes move on. The real screen is small
 * and its watchers are four, so nothing that matters is drawn smaller than two pixels (four on the screen), the
 * characters fill a good part of the picture, and every right note makes a burst of sparkles round what it moved. Buns come out of the
 * oven, the lamb follows Mary to school, the stars come out, the boat crosses the river, the bridge is built. A
 * right note makes the scene's subject hop and sparkle, and a finished song throws confetti. Plain C with integer
 * arithmetic only and no C library, as the FM-1 firmware draws the same pictures. */
#include "RhymesCore.h"
#include "internal.h"

#define W RH_SCENE_W
#define H RH_SCENE_H

#define INK 0x1A1A1Au
#define WHITE 0xFFFFFFu
#define SKIN 0xF5CBA7u

static uint32_t *px;

static void put(int x, int y, uint32_t c)
{
    if (x >= 0 && x < W && y >= 0 && y < H)
        px[y * W + x] = c;
}

static void rect(int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            put(i, j, c);
}

static void frame(int x, int y, int w, int h, uint32_t c)
{
    rect(x, y, w, 1, c);
    rect(x, y + h - 1, w, 1, c);
    rect(x, y, 1, h, c);
    rect(x + w - 1, y, 1, h, c);
}

/* A filled box with an ink outline. */
static void box(int x, int y, int w, int h, uint32_t c)
{
    rect(x, y, w, h, c);
    frame(x, y, w, h, INK);
}

static int in_ellipse(int x, int y, int cx, int cy, int rx, int ry)
{
    int dx = x - cx, dy = y - cy;
    if (rx <= 0 || ry <= 0)
        return dx == 0 && dy == 0;
    return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry;
}

static void disc(int cx, int cy, int rx, int ry, uint32_t c)
{
    int x, y;
    for (y = cy - ry; y <= cy + ry; y++)
        for (x = cx - rx; x <= cx + rx; x++)
            if (in_ellipse(x, y, cx, cy, rx, ry))
                put(x, y, c);
}

static void ring(int cx, int cy, int rx, int ry, uint32_t c)
{
    int x, y;
    for (y = cy - ry; y <= cy + ry; y++)
        for (x = cx - rx; x <= cx + rx; x++)
            if (in_ellipse(x, y, cx, cy, rx, ry) &&
                (!in_ellipse(x - 1, y, cx, cy, rx, ry) || !in_ellipse(x + 1, y, cx, cy, rx, ry) ||
                 !in_ellipse(x, y - 1, cx, cy, rx, ry) || !in_ellipse(x, y + 1, cx, cy, rx, ry)))
                put(x, y, c);
}

/* A filled ellipse with an ink outline. */
static void blob(int cx, int cy, int rx, int ry, uint32_t c)
{
    disc(cx, cy, rx, ry, c);
    ring(cx, cy, rx, ry, INK);
}

/* The top half of a blob: an umbrella. */
static void dome(int cx, int cy, int rx, int ry, uint32_t c)
{
    int x, y;
    for (y = cy - ry; y <= cy; y++)
        for (x = cx - rx; x <= cx + rx; x++)
            if (in_ellipse(x, y, cx, cy, rx, ry))
                put(x, y, y == cy || !in_ellipse(x, y - 1, cx, cy, rx, ry) || !in_ellipse(x - 1, y, cx, cy, rx, ry) ||
                              !in_ellipse(x + 1, y, cx, cy, rx, ry)
                          ? INK
                          : c);
}

static int iabs(int v) { return v < 0 ? -v : v; }

static void line(int x0, int y0, int x1, int y1, uint32_t c);

/* A line two pixels wide. */
static void thick(int x0, int y0, int x1, int y1, uint32_t c)
{
    line(x0, y0, x1, y1, c);
    line(x0 + 1, y0, x1 + 1, y1, c);
    line(x0, y0 + 1, x1, y1 + 1, c);
}

static void line(int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = iabs(x1 - x0), dy = -iabs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy, e2;
    for (;;) {
        put(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y0 += sy;
        }
    }
}

/* A roof or a tree: rows narrowing from the base at y0 up to a point at the top. */
static void peak(int cx, int top, int half, int base, uint32_t c)
{
    int y, h = base - top;
    for (y = top; y <= base; y++) {
        int w = h > 0 ? half * (y - top) / h : half;
        rect(cx - w, y, 2 * w + 1, 1, c);
        put(cx - w, y, INK);
        put(cx + w, y, INK);
    }
}

static uint32_t hash(int a, int b)
{
    uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static uint32_t mix(uint32_t a, uint32_t b, int t) /* t of 16 towards b */
{
    uint32_t out = 0;
    int s;
    if (t < 0)
        t = 0;
    if (t > 16)
        t = 16;
    for (s = 0; s <= 16; s += 8) {
        int ca = (int)((a >> s) & 0xFFu), cb = (int)((b >> s) & 0xFFu);
        out |= (uint32_t)(ca + (cb - ca) * t / 16) << s;
    }
    return out;
}

static void sky(uint32_t top, uint32_t bottom, int rows)
{
    int y;
    for (y = 0; y < rows; y++)
        rect(0, y, W, 1, mix(top, bottom, y * 16 / (rows > 1 ? rows - 1 : 1)));
}

/* A four-pointed sparkle. */
static void sparkle(int x, int y, int r, uint32_t c)
{
    int i;
    for (i = -r; i <= r; i++) {
        put(x + i, y, c);
        put(x, y + i, c);
    }
    if (r >= 2)
        rect(x - 1, y - 1, 3, 3, c);
}

/* A five-pointed star of radius r: its ten corners for a radius of 16, round from the top. */
static const int8_t STAR_X[10] = {0, 4, 15, 6, 9, 0, -9, -6, -15, -4};
static const int8_t STAR_Y[10] = {-16, -5, -5, 2, 13, 7, 13, 2, -5, -5};

static int in_star(int x, int y, int r)
{
    int i, j, in = 0;
    x *= 16, y *= 16;
    for (i = 0, j = 9; i < 10; j = i++) {
        int xi = STAR_X[i] * r, yi = STAR_Y[i] * r, xj = STAR_X[j] * r, yj = STAR_Y[j] * r;
        int a = (x - xi) * (yj - yi), b = (xj - xi) * (y - yi);
        if ((yi > y) != (yj > y) && (yj > yi ? a < b : a > b))
            in = !in;
    }
    return in;
}

static void star(int cx, int cy, int r, uint32_t c)
{
    int x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (in_star(x, y, r))
                put(cx + x, cy + y, c);
}

static void cloud(int x, int y, uint32_t c)
{
    blob(x, y, 7, 4, c);
    blob(x + 7, y - 2, 6, 4, c);
    blob(x + 13, y + 1, 6, 3, c);
    rect(x - 3, y + 1, 20, 3, c);
}

static void sun(int x, int y, int r, int rays, int step)
{
    int i;
    static const int DX[8] = {3, 2, 0, -2, -3, -2, 0, 2}, DY[8] = {0, 2, 3, 2, 0, -2, -3, -2};
    if (rays)
        for (i = (step & 1); i < 8; i += 2)
            line(x + DX[i] * (r + 1) / 3, y + DY[i] * (r + 1) / 3, x + DX[i] * (r + 4) / 3, y + DY[i] * (r + 4) / 3,
                 0xF5B700u);
    blob(x, y, r, r, 0xFFD43Bu);
}

/* Letters for the pictures' words: a few 5 by 5 glyphs, a row of bits each, each bit two pixels square. */
static void glyph(int x, int y, char ch, uint32_t c)
{
    static const uint8_t M[5] = {17, 27, 21, 17, 17}, O[5] = {14, 17, 17, 17, 14}, Z[5] = {31, 2, 4, 8, 31},
                         X[5] = {4, 4, 4, 0, 4};
    const uint8_t *g = ch == 'M' ? M : ch == 'O' ? O : ch == 'Z' ? Z : X;
    int i, j;
    for (j = 0; j < 5; j++)
        for (i = 0; i < 5; i++)
            if (g[j] >> (4 - i) & 1)
                rect(x + i * 2, y + j * 2, 2, 2, c);
}

static void word(int x, int y, const char *s, uint32_t c)
{
    for (; *s; s++, x += 12)
        glyph(x, y, *s, c);
}

/* A speech bubble round a word. */
static void bubble(int x, int y, const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    box(x, y, n * 12 + 4, 15, WHITE);
    rect(x + 4, y + 15, 2, 2, INK);
    rect(x + 2, y + 17, 2, 2, INK);
    word(x + 3, y + 3, s, INK);
}

static int hop(const rh_view_t *v) { return v->hit < 3 ? (3 - v->hit) * 2 : 0; }
static int wobble(const rh_view_t *v) { return v->miss < 4 ? ((v->miss & 1) ? 1 : -1) : 0; }
/* How far through the song, 0 .. span. */
static int prog(const rh_view_t *v, int span) { return v->count > 0 ? v->pos * span / v->count : 0; }
static int line_prog(const rh_view_t *v, int span) { return v->line_len > 0 ? v->line_pos * span / v->line_len : 0; }
/* In a finished song, everyone jumps for joy. */
static int joy(const rh_view_t *v, int step, int who) { return v->done >= 0 && ((step + who) & 3) < 2 ? 3 : 0; }

/* A child about 28 pixels tall, from the feet up: x is the middle, y the feet. */
static void child(int x, int y, uint32_t clothes, uint32_t hair, int stride)
{
    rect(x - 3, y - 5, 2, 5, INK);
    rect(x + 2, y - 5, 2, 5, INK);
    if (stride) {
        rect(x - 5, y - 2, 2, 2, INK);
        rect(x + 4, y - 2, 2, 2, INK);
    }
    peak(x, y - 16, 6, y - 5, clothes);
    blob(x, y - 21, 5, 5, SKIN);
    disc(x, y - 25, 5, 2, hair);
    rect(x - 2, y - 22, 1, 2, INK);
    rect(x + 2, y - 22, 1, 2, INK);
    rect(x - 1, y - 18, 3, 1, INK);
    put(x - 2, y - 19, INK);
    put(x + 2, y - 19, INK);
}

/* ------------------------------------------------------------------ the songs */

static void bakery(const rh_view_t *v, int step, int *sx, int *sy)
{
    int i, y, shown = prog(v, 8);
    rect(0, 0, W, 44, 0xF6E3C4u);
    for (y = 6; y < 44; y += 8)
        rect(0, y, W, 1, 0xEBD3AEu);
    /* the window */
    box(84, 5, 28, 20, 0xBFE3F5u);
    cloud(90, 12, WHITE);
    rect(97, 5, 1, 20, INK);
    rect(84, 15, 28, 1, INK);
    /* the oven, glowing */
    box(4, 6, 40, 38, 0xB5523Bu);
    for (y = 12; y < 44; y += 6)
        rect(5, y, 38, 1, 0x8E3B28u);
    disc(24, 30, 12, 10, 0x3A1E14u);
    rect(12, 30, 25, 11, 0x3A1E14u);
    rect(14, 36, 21, 4, (step & 1) ? 0xF39C12u : 0xE67E22u);
    rect(17, 34, 4, 2, 0xF7DC6Fu);
    rect(26 + (step & 1), 34, 4, 2, 0xF7DC6Fu);
    /* the counter and the tray */
    rect(0, 44, W, H - 44, 0xA0703Fu);
    rect(0, 44, W, 1, INK);
    for (i = 0; i < 4; i++)
        rect(0, 50 + i * 4, W, 1, 0x8A5E33u);
    box(48, 41, 68, 4, 0xC9CED3u);
    *sx = 24, *sy = 26;
    for (i = 0; i < shown && i < 8; i++) {
        int bx = 57 + (i % 4) * 15 + (i >= 4 ? 7 : 0), by = i < 4 ? 37 : 30;
        int up = (i == shown - 1 ? hop(v) : 0) + joy(v, step, i);
        blob(bx, by - up, 7, 4, 0xC68642u);
        disc(bx - 3, by - 2 - up, 2, 1, 0xE0A060u);
        rect(bx - 5, by - 1 - up, 11, 2, 0xFFF8E7u);
        rect(bx - 1, by - 4 - up, 2, 8, 0xFFF8E7u);
        if (i == shown - 1) {
            *sx = bx, *sy = by - 6;
            if (v->hit < 8) /* steam */
                for (y = 0; y < 3; y++)
                    rect(bx - 3 + y * 2 + ((step + y) & 1) * 2, by - 9 - y * 3 - (step % 3), 2, 2, 0xFFFFFFu);
        }
    }
}

static void rain(const rh_view_t *v, int step, int *sx, int *sy)
{
    int p = v->done >= 0 ? 16 : prog(v, 16), i, drops = 30 * (16 - p) / 16;
    uint32_t grey = mix(0x6C7A89u, WHITE, p);
    sky(mix(0x7F8C99u, 0x7FCBF2u, p), mix(0xB0BAC3u, 0xD5F0FCu, p), 46);
    sun(96, 40 - p * 26 / 16, 7, v->done >= 0, step);
    if (v->done >= 0) { /* a rainbow */
        static const uint32_t BOW[6] = {0xE53935u, 0xFB8C00u, 0xFDD835u, 0x43A047u, 0x1E88E5u, 0x8E24AAu};
        int x, y, r;
        for (r = 0; r < 6; r++)
            for (y = 6; y < 46; y++)
                for (x = 20; x < 100; x++) {
                    int d = (x - 60) * (x - 60) + (y - 46) * (y - 46), ro = 38 - r * 2, ri = ro - 2;
                    if (d < ro * ro && d >= ri * ri)
                        put(x, y, BOW[r]);
                }
    }
    cloud(16 - p * 22 / 16, 9, grey);
    cloud(50, 6 - p * 10 / 16, grey);
    cloud(86 + p * 24 / 16, 11, grey);
    rect(0, 46, W, H - 46, 0x7CB342u);
    rect(0, 46, W, 1, INK);
    /* the puddles dry up */
    disc(24, 55, 10 - p / 2, 2, 0x5DADE2u);
    disc(92, 58, 8 - p / 2, 2, 0x5DADE2u);
    for (i = 0; i < drops; i++) {
        uint32_t h = hash(i, 7);
        int x = (int)(h % W), y = (int)((h >> 8) % 46u + (uint32_t)step * 4u) % 46;
        rect(x, y, 2, 4, 0x5DADE2u);
    }
    /* a little house, and a child with an umbrella */
    box(4, 30, 18, 16, 0xFFF3E0u);
    peak(13, 22, 11, 30, 0xC0392Bu);
    box(10, 36, 6, 10, 0x8D6E63u);
    {
        int cx = 62, up = hop(v) + joy(v, step, 0), w = wobble(v);
        child(cx + w, 56 - up, 0xFF7043u, 0x6D4C41u, v->hit < 2);
        if (v->done < 0) { /* the umbrella, put down once the sun is out */
            rect(cx + 6 + w, 25 - up, 2, 17, INK);
            dome(cx + 6 + w, 25 - up, 13, 7, 0xFDD835u);
        }
        *sx = 96, *sy = 40 - p * 26 / 16;
    }
}

static void lamb(int x, int y, int up)
{
    int i;
    y -= up;
    rect(x - 6, y - 5, 2, 5, INK);
    rect(x + 4, y - 5, 2, 5, INK);
    blob(x, y - 10, 9, 6, WHITE);
    for (i = -6; i <= 6; i += 6)
        blob(x + i, y - 16, 3, 2, WHITE);
    disc(x, y - 11, 8, 4, WHITE);
    blob(x + 10, y - 13, 4, 3, 0x3A3A3Au);
    rect(x + 11, y - 14, 2, 2, WHITE);
}

static void meadow(const rh_view_t *v, int step, int *sx, int *sy)
{
    int p = v->done >= 0 ? 16 : prog(v, 16), x = 22 + p * 54 / 16, i;
    sky(0x9ED8F5u, 0xE3F4FCu, 44);
    sun(12, 10, 6, 0, step);
    cloud(40 + (step / 8) % 30, 8, WHITE);
    disc(30, 50, 46, 14, 0x9CCC65u);
    rect(0, 44, W, H - 44, 0x7CB342u);
    rect(0, 50, W, 4, 0xE9D8A6u);
    for (i = 0; i < 18; i++) {
        uint32_t h = hash(i, 3);
        rect((int)(h % W), 56 + (int)((h >> 8) % 6u), 2, 2, (h >> 12) & 1 ? 0xF8BBD0u : 0xFFF59Du);
    }
    /* the school, with its bell */
    box(90, 22, 26, 26, 0xD35400u);
    peak(103, 10, 15, 22, 0x8E2B1Du);
    box(100, 36, 7, 12, 0x6D4C41u);
    box(93, 27, 5, 5, 0xBFE3F5u);
    box(109, 27, 5, 5, 0xBFE3F5u);
    blob(103, 17, 2, 2, 0xF1C40Fu);
    /* Mary, and her lamb behind */
    child(x + wobble(v), 52 - hop(v) - joy(v, step, 0), 0x4FC3F7u, 0xF4D03Fu, v->hit < 3 && (step & 1));
    lamb(x - 20, 53, hop(v) + joy(v, step, 2));
    *sx = x - 20, *sy = 36;
    if (v->done >= 0)
        for (i = 0; i < 3; i++) { /* hearts float up */
            int hx = x - 26 + i * 10, hy = 34 - ((v->done + i * 3) % 16);
            rect(hx - 2, hy, 2, 2, 0xE84393u);
            rect(hx + 1, hy, 2, 2, 0xE84393u);
            rect(hx - 2, hy + 2, 5, 2, 0xE84393u);
            rect(hx - 1, hy + 4, 3, 1, 0xE84393u);
        }
}

static void night(const rh_view_t *v, int step, int *sx, int *sy)
{
    int i, r;
    sky(0x0B1A3Au, 0x2B4170u, H);
    disc(16, 12, 7, 7, 0xFFF3B0u);
    disc(19, 10, 6, 6, mix(0x0B1A3Au, 0x2B4170u, 3));
    for (i = 0; i < 42; i++) {
        uint32_t h = hash(i, 11);
        int x = 4 + (int)(h % 112u), y = 2 + (int)((h >> 8) % 40u);
        if (x > 46 && x < 74 && y < 34)
            x = x < 60 ? x - 16 : x + 16;
        if (i < v->pos || v->done >= 0) {
            int tw = ((step + i) % 6) == 0;
            sparkle(x, y, tw || i == v->pos - 1 ? 3 : 2, tw ? WHITE : 0xFFF3B0u);
        } else {
            rect(x, y, 2, 2, 0x4A5A80u);
        }
    }
    /* the little star, smiling, and bigger and brighter while a note sounds */
    r = 11 + (v->sounding ? 2 : 0) + (v->hit < 2 ? 1 : 0);
    star(60, 22, r + 2, 0x3E4E7Au);
    star(60, 22, r, v->sounding || v->hit < 2 ? 0xFFE066u : 0xFFD43Bu);
    rect(56, 20, 2, 3, INK);
    rect(63, 20, 2, 3, INK);
    rect(57, 25, 1, 1, INK);
    rect(58, 26, 5, 1, INK);
    rect(63, 25, 1, 1, INK);
    rect(54, 24, 2, 2, 0xFFA8A8u);
    rect(65, 24, 2, 2, 0xFFA8A8u);
    *sx = 60, *sy = 20;
    /* the hills, and a house with its light on */
    disc(20, 70, 50, 16, 0x1B2A41u);
    disc(96, 72, 44, 18, 0x15233Au);
    rect(84, 46, 16, 10, 0x2C3E50u);
    peak(92, 39, 10, 46, 0x1B2A41u);
    rect(89, 49, 5, 4, 0xFFD54Fu);
    if (v->done >= 0) { /* a shooting star */
        int x = (v->done * 7) % 150 - 20, y = 4 + x / 6;
        for (i = 0; i < 14; i++)
            rect(x - i, y - i / 3, 1, 2, mix(WHITE, 0x2B4170u, i * 16 / 14));
        sparkle(x, y, 3, WHITE);
    }
}

static void river(const rh_view_t *v, int step, int *sx, int *sy)
{
    int p = v->done >= 0 ? 16 : prog(v, 16), x = 2 + p * 70 / 16, i, y = 46 - joy(v, step, 0), dip = v->hit < 3;
    sky(0x9ED8F5u, 0xE0F4FFu, 30);
    sun(104, 9, 5, 0, step);
    cloud(20, 8, WHITE);
    rect(0, 28, W, 8, 0x6AAE5Au);
    for (i = 0; i < 9; i++) {
        int tx = 6 + i * 14;
        rect(tx, 22, 1, 7, 0x6D4C41u);
        blob(tx, 21, 4, 4, i & 1 ? 0x2E7D32u : 0x43A047u);
    }
    rect(0, 36, W, H - 36, 0x3FA7D6u);
    rect(0, 36, W, 1, INK);
    for (i = 0; i < 16; i++) {
        int wx = (i * 23 + step * 2) % (W + 8) - 4, wy = 40 + (i * 7) % 22;
        rect(wx, wy, 6, 2, 0x8FD3F4u);
    }
    /* the boat, rowing */
    child(x + 17, y + 2, 0xFF7043u, 0x6D4C41u, 0);
    thick(x + 17, y - 8, dip ? x + 32 : x + 30, dip ? y + 6 : y - 4, 0x8D5524u);
    if (dip && v->hit < 2)
        ring(x + 33, y + 7, 5, 2, WHITE);
    for (i = 0; i < 8; i++)
        rect(x + i, y + i, 34 - 2 * i, 1, i < 2 ? INK : 0x8D5524u);
    rect(x + 8, y + 8, 18, 1, INK);
    thick(x, y, x + 7, y + 7, INK);
    thick(x + 32, y, x + 25, y + 7, INK);
    *sx = x + 17, *sy = y - 20;
    if (v->done >= 0) { /* a fish jumps */
        int t = v->done % 12, fx = 30 + t * 3, fy = 46 - (t * (12 - t)) / 3;
        blob(fx, fy, 5, 3, 0xFF8A65u);
        peak(fx - 7, fy - 3, 3, fy + 3, 0xFF8A65u);
        rect(fx + 2, fy - 1, 2, 2, INK);
    }
}

static void dawn(const rh_view_t *v, int step, int *sx, int *sy)
{
    int p = v->done >= 0 ? 16 : prog(v, 16), swing = v->hit < 4 ? ((v->hit & 1) ? 2 : -2) : 0, i;
    sky(mix(0x2C1E4Au, 0x8FC9F0u, p), mix(0x4B3A73u, 0xFFE0B2u, p), 50);
    sun(100, 56 - p * 34 / 16, 7, v->done >= 0, step);
    rect(0, 50, W, H - 50, 0x5D8A4Eu);
    rect(0, 50, W, 1, INK);
    /* the bell tower */
    box(12, 14, 34, 36, 0xB8B0A0u);
    peak(29, 1, 19, 14, 0x7B3F2Eu);
    rect(18, 18, 22, 20, 0x3B3B3Bu);
    rect(28 + swing / 2, 18, 2, 4, INK);
    blob(29 + swing, 26, 6, 5, 0xE0B341u);
    rect(23 + swing, 26, 13, 7, 0xE0B341u);
    rect(22 + swing, 32, 15, 2, INK);
    rect(28 + swing, 34, 3, 3, INK);
    for (i = 0; i < 3; i++)
        box(19 + i * 8, 42, 4, 6, 0x5D5348u);
    *sx = 29 + swing, *sy = 24;
    if (v->hit < 3) { /* ding */
        thick(41, 22, 47, 18, WHITE);
        thick(41, 28, 48, 28, WHITE);
        thick(16, 22, 10, 18, WHITE);
    }
    /* Brother John's house */
    box(64, 30, 36, 20, 0xF0E0C0u);
    peak(82, 18, 20, 30, 0xC0392Bu);
    box(70, 33, 22, 15, v->done >= 0 ? 0xFFE082u : 0x2E3A59u);
    blob(78, 41, 4, 4, SKIN);
    peak(78, 34, 5, 38, 0xC0392Bu);
    rect(84, 42, 7, 4, WHITE);
    if (v->done >= 0) {
        rect(76, 40, 1, 2, INK);
        rect(80, 40, 1, 2, INK);
        rect(77, 43, 3, 1, INK);
    } else {
        rect(75, 41, 2, 1, INK);
        rect(79, 41, 2, 1, INK);
        for (i = 0; i < 3; i++) { /* he snores */
            int t = (step / 2 + i * 3) % 9;
            glyph(92 + i * 9 + t / 3, 24 - t * 2, 'Z', mix(WHITE, 0x8FC9F0u, t * 2));
        }
    }
    if (v->done >= 0)
        for (i = 0; i < 3; i++) { /* birds */
            int bx = (v->done * 2 + i * 30) % 140 - 10, by = 10 + i * 5;
            thick(bx - 3, by - 2, bx, by, INK);
            thick(bx, by, bx + 3, by - 2, INK);
        }
}

static void bridge(const rh_view_t *v, int step, int *sx, int *sy)
{
    int laid = v->done >= 0 ? 12 : prog(v, 12), i;
    sky(0xBFDDF2u, 0xF2F8FCu, 44);
    cloud(54, 8, WHITE);
    /* the city behind */
    for (i = 0; i < 12; i++) {
        uint32_t h = hash(i, 5);
        int bh = 6 + (int)(h % 12u);
        rect(i * 10, 40 - bh, 9, bh, 0xA9B4BFu);
    }
    rect(0, 44, W, H - 44, 0x4F8FBFu);
    rect(0, 44, W, 1, INK);
    for (i = 0; i < 12; i++)
        rect((i * 29 + step * 2) % (W + 6) - 3, 48 + (i * 5) % 14, 6, 2, 0x8EC5E8u);
    /* the two towers, with flags */
    for (i = 0; i < 2; i++) {
        int tx = i ? 96 : 10;
        box(tx, 12, 12, 40, 0xC9BFA7u);
        peak(tx + 6, 3, 6, 12, 0x5D6D7Eu);
        box(tx + 4, 18, 4, 7, 0x5D6D7Eu);
        rect(tx + 6, 0, 1, 4, INK);
        rect(tx + 7, 0, 3 + ((step + i) & 1), 2, 0xC0392Bu);
    }
    /* the stones, laid one by one from the left; the newest drops in */
    for (i = 0; i < laid; i++) {
        int bx = 22 + (i % 6) * 12, by = i < 6 ? 32 : 25;
        if (i == laid - 1 && v->hit < 2)
            by -= 8 - v->hit * 4;
        by -= joy(v, step, i) / 2;
        box(bx, by, 12, 7, i & 1 ? 0xD5CBB1u : 0xC2B694u);
        rect(bx + 2, by + 2, 3, 2, i & 1 ? 0xE4DCC6u : 0xD3C9A8u);
        if (i == laid - 1)
            *sx = bx + 6, *sy = by - 3;
    }
    if (laid == 0)
        *sx = 28, *sy = 28;
    if (laid >= 6)
        for (i = 0; i < 3; i++) /* the arches below */
            box(30 + i * 24, 39, 6, 7, 0xB0A386u);
    if (v->done >= 0) { /* a boat goes under */
        int bx = (v->done * 3) % 150 - 20;
        box(bx, 50, 20, 5, 0xC0392Bu);
        rect(bx + 8, 42, 2, 8, INK);
        peak(bx + 13, 42, 4, 48, WHITE);
    }
}

static void cow(int x, int y, int up)
{
    y -= up;
    rect(x - 7, y - 4, 2, 4, INK);
    rect(x - 3, y - 4, 2, 4, INK);
    rect(x + 2, y - 4, 2, 4, INK);
    rect(x + 6, y - 4, 2, 4, INK);
    blob(x, y - 8, 9, 5, WHITE);
    disc(x - 3, y - 9, 2, 2, 0x2D2D2Du);
    disc(x + 4, y - 7, 2, 1, 0x2D2D2Du);
    blob(x - 11, y - 11, 3, 3, WHITE);
    disc(x - 12, y - 9, 2, 1, 0xF8BBD0u);
    rect(x - 12, y - 13, 2, 2, INK);
    rect(x - 14, y - 16, 2, 3, INK);
    rect(x - 9, y - 16, 2, 3, INK);
    thick(x + 9, y - 9, x + 11, y - 4, INK);
}

static void farm(const rh_view_t *v, int step, int *sx, int *sy)
{
    int i, chicks = 0, cowx = 0;
    static const int EIEIO[4] = {1, 3, 7, 99};
    sky(0x9ED8F5u, 0xE3F4FCu, 42);
    sun(104, 9, 5, v->done >= 0, step);
    cloud(46, 8, WHITE);
    rect(0, 42, W, H - 42, 0x8BC34Au);
    for (i = 0; i < 4; i++)
        rect(0, 48 + i * 4, W, 1, 0x7CB342u);
    /* the barn */
    box(4, 18, 34, 26, 0xC0392Bu);
    peak(21, 6, 19, 18, 0x7B241Cu);
    box(14, 28, 14, 16, 0xC0392Bu);
    line(14, 28, 27, 43, WHITE);
    line(27, 28, 14, 43, WHITE);
    box(17, 12, 8, 5, 0xF4D03Fu);
    /* the fence */
    for (i = 44; i < W; i += 9)
        box(i, 34, 3, 10, 0xF5F5F5u);
    rect(44, 36, W - 44, 2, 0xF5F5F5u);
    rect(44, 40, W - 44, 2, 0xF5F5F5u);
    /* the farmer */
    child(48, 56 - hop(v) - joy(v, step, 0), 0x2E86C1u, 0xF4D03Fu, 0);
    rect(40, 30 - hop(v) - joy(v, step, 0), 17, 2, 0xF4D03Fu);
    rect(44, 26 - hop(v) - joy(v, step, 0), 9, 4, 0xF4D03Fu);
    *sx = 48, *sy = 36;
    /* the cow comes in while he sings of it, and moos */
    if (v->line >= 2 || v->done >= 0) {
        cowx = v->line == 2 && v->done < 0 ? 128 - line_prog(v, 40) : 88;
        cow(cowx, 58, joy(v, step, 1));
        if ((v->line == 4 || v->line == 5) && v->hit < 4 && v->done < 0)
            bubble(cowx - 14, 12, "MOO");
        if (v->line >= 4 || v->done >= 0)
            *sx = cowx - 10, *sy = 44;
    }
    /* a chick for every E-I-E-I-O sung */
    for (i = 0; i < 3; i++)
        if (v->line > EIEIO[i] || (v->done >= 0 && v->line >= EIEIO[i]))
            chicks++;
    for (i = 0; i < chicks; i++) {
        int cx = 60 + i * 12, up = joy(v, step, i + 2);
        blob(cx, 59 - up, 5, 3, 0xFFE066u);
        blob(cx + 3, 54 - up, 3, 3, 0xFFE066u);
        rect(cx + 6, 54 - up, 3, 2, 0xF39C12u);
        rect(cx + 3, 53 - up, 1, 2, INK);
    }
    (void)wobble;
}

static void spider_at(int x, int y, int step)
{
    int i, s = step & 1;
    for (i = 0; i < 4; i++) {
        int ly = y - 5 + i * 3, k = (i + s) & 1;
        thick(x - 3, ly, x - 10, ly - 3 + k * 5, INK);
        thick(x + 3, ly, x + 10, ly - 3 + k * 5, INK);
    }
    disc(x, y, 5, 6, 0x222222u);
    disc(x, y - 8, 4, 3, 0x222222u);
    rect(x - 3, y - 10, 2, 3, WHITE);
    rect(x + 2, y - 10, 2, 3, WHITE);
    rect(x - 2, y - 9, 1, 2, INK);
    rect(x + 2, y - 9, 1, 2, INK);
    rect(x - 1, y - 5, 3, 1, 0xE84393u);
}

static void spout(const rh_view_t *v, int step, int *sx, int *sy)
{
    int lp = line_prog(v, 16), y = 54, i, raining = 0, sunny = 0, sy0;
    switch (v->done >= 0 ? 4 : v->line) {
    case 0: y = 54 - lp * 44 / 16; break;
    case 1: y = 10 + lp * 44 / 16; raining = 1; break;
    case 2: y = 54; sunny = lp; break;
    case 3: y = 54 - lp * 44 / 16; sunny = 16; break;
    default: y = 10; sunny = 16; break;
    }
    if (v->line == 0 && v->done < 0)
        sunny = 10;
    sky(mix(0x7F8C99u, 0x8ED6F7u, raining ? 0 : sunny), mix(0xA4AFB9u, 0xDDF3FDu, raining ? 0 : sunny), 58);
    sun(100, 34 - sunny * 22 / 16, 7, v->done >= 0, step);
    if (raining || sunny < 12)
        cloud(84, 10, raining ? 0x6C7A89u : 0xD5DCE2u);
    /* the house wall and its gutter and spout */
    rect(0, 0, 50, 58, 0xF5E6CCu);
    for (i = 4; i < 58; i += 6)
        rect(0, i, 50, 1, 0xE8D5B5u);
    rect(50, 0, 1, 58, INK);
    box(26, 2, 40, 5, 0x95A5A6u);
    box(55, 6, 11, 50, 0x95A5A6u);
    rect(57, 7, 3, 48, 0xBDC3C7u);
    box(56, 52, 16, 6, 0x95A5A6u);
    rect(0, 58, W, H - 58, 0x6AB04Cu);
    rect(0, 58, W, 1, INK);
    if (raining) {
        for (i = 0; i < 34; i++) {
            uint32_t h = hash(i, 13);
            int x = (int)(h % W), ry = (int)((h >> 8) % 58u + (uint32_t)step * 4u) % 58;
            rect(x, ry, 2, 4, 0x5DADE2u);
        }
        for (i = 0; i < 4; i++) /* water pours from the spout */
            rect(72 + i * 2, 53 + ((step + i) % 3), 2, 3, 0x5DADE2u);
    }
    disc(84, 61, 10 - (sunny > 8 ? (sunny - 8) : 0), 1, 0x5DADE2u);
    /* the spider, climbing or washed down; its thread above it */
    sy0 = (y < 14 ? 14 : y > 50 ? 50 : y) - hop(v) / 2;
    rect(60, 7, 1, sy0 - 18 > 0 ? sy0 - 18 : 0, WHITE);
    spider_at(60 + wobble(v), sy0, v->hit < 4 ? step : 0);
    *sx = 60, *sy = sy0 - 10;
    if (v->done >= 0) /* waving from the top */
        thick(70, sy0 - 4, 75, sy0 - 11 - (step & 1) * 2, INK);
}

static void sleigh(const rh_view_t *v, int step, int *sx, int *sy)
{
    int p = v->done >= 0 ? 16 : prog(v, 16), x = 2 + p * 60 / 16, i, y = 52, gallop = step & 1;
    int lift = v->done >= 0 ? (v->done < 20 ? v->done : 20) : 0, jig = v->hit < 3 ? (v->hit & 1) : 0;
    sky(0x15203Au, 0x3D5A80u, 50);
    for (i = 0; i < 14; i++) {
        uint32_t h = hash(i, 17);
        rect((int)(h % W), (int)((h >> 8) % 30u), 2, 2, (step + i) % 7 ? 0x9FB3C8u : WHITE);
    }
    disc(100, 10, 5, 5, 0xFFF3B0u);
    disc(30, 60, 50, 18, 0xE3ECF3u);
    disc(100, 62, 40, 20, 0xF4F8FBu);
    rect(0, 50, W, H - 50, 0xF4F8FBu);
    for (i = 0; i < 5; i++) { /* pine trees with snow on */
        int tx = 10 + i * 26 + (i & 1) * 6, ty = 30 + (i & 1) * 6;
        rect(tx, ty + 12, 2, 4, 0x5D4037u);
        peak(tx, ty, 6, ty + 12, 0x1E5631u);
        peak(tx, ty, 2, ty + 4, WHITE);
    }
    y -= lift;
    /* the horse */
    rect(x + 32 + gallop * 2, y - 8, 2, 8, INK);
    rect(x + 37 - gallop * 2, y - 8, 2, 8, INK);
    rect(x + 44 + gallop * 2, y - 8, 2, 8, INK);
    rect(x + 49 - gallop * 2, y - 8, 2, 8, INK);
    blob(x + 41, y - 13, 11, 6, 0x8B5A2Bu);
    blob(x + 53, y - 21, 4, 5, 0x8B5A2Bu);
    rect(x + 51, y - 27, 2, 3, 0x8B5A2Bu);
    rect(x + 54, y - 22, 2, 2, INK);
    rect(x + 47, y - 24, 3, 8, 0x5D3A1Au);
    for (i = 0; i < 3; i++) /* the bells on its harness */
        blob(x + 34 + i * 6, y - 9 - jig * 2, 2, 2, 0xF1C40Fu);
    thick(x + 22, y - 12, x + 32, y - 12, INK);
    /* the sleigh */
    child(x + 12, y - 7, 0x2E7D32u, 0xC0392Bu, 0);
    box(x, y - 13, 26, 10, 0xC0392Bu);
    rect(x + 1, y - 13, 24, 2, 0xF1C40Fu);
    rect(x, y - 2, 30, 2, 0xF1C40Fu);
    rect(x + 29, y - 5, 2, 3, 0xF1C40Fu);
    *sx = x + 40, *sy = y - 22;
    for (i = 0; i < 30; i++) { /* snow falling */
        uint32_t h = hash(i, 19);
        int fx = (int)((h % W + (uint32_t)step) % W), fy = (int)(((h >> 8) % 64u + (uint32_t)step * 2u) % 64u);
        rect(fx, fy, 2, 2, WHITE);
    }
}

static void party(const rh_view_t *v, int step, int *sx, int *sy)
{
    static const uint32_t FLAGS[4] = {0xE53935u, 0xFDD835u, 0x1E88E5u, 0x43A047u};
    int lit = v->done >= 0 ? 4 : v->line, i, x;
    rect(0, 0, W, 46, 0xFDE2E4u);
    for (x = 0; x < W; x += 12)
        rect(x, 0, 6, 46, 0xFAD0D6u);
    /* bunting, swaying */
    for (i = 0; i < 10; i++) {
        int bx = 4 + i * 12, by = 4 + (i & 1) + ((step + i) % 4 == 0);
        peak(bx + 4, by, 3, by + 6, FLAGS[i & 3]);
        rect(bx + 1, by + 6, 7, 1, FLAGS[i & 3]);
    }
    rect(0, 5, W, 1, INK);
    rect(0, 46, W, H - 46, 0xE8C39Eu);
    rect(0, 46, W, 2, WHITE);
    rect(0, 48, W, 1, INK);
    /* a present */
    box(8, 34, 14, 12, 0x1E88E5u);
    rect(14, 34, 2, 12, 0xFDD835u);
    rect(8, 39, 14, 2, 0xFDD835u);
    /* the cake */
    box(32, 44, 56, 3, WHITE);
    box(36, 32, 48, 13, 0xF8BBD0u);
    box(44, 22, 32, 11, 0xFFF3E0u);
    for (x = 37; x < 84; x += 5)
        rect(x, 33, 3, 2 + ((x / 5) & 1), WHITE);
    for (i = 0; i < 6; i++)
        disc(40 + i * 8, 38, 1, 1, FLAGS[i & 3]);
    /* a candle for each line sung */
    for (i = 0; i < 4; i++) {
        int cx = 47 + i * 8;
        box(cx, 12, 5, 10, i & 1 ? 0x81D4FAu : 0xFFF59Du);
        if (i < lit) {
            int f = (step + i) & 1;
            disc(cx + 2, 7 - f, 2, 3 + f, 0xFFB300u);
            rect(cx + 2, 7, 1, 3, 0xFFF59Du);
        }
    }
    *sx = 47 + (lit < 4 ? lit : 3) * 8 + 2, *sy = 6;
    /* Teddy, in a party hat */
    {
        int tx = 102, ty = 44 - joy(v, step, 0) - hop(v);
        blob(tx, ty - 6, 7, 6, 0xA0522Du);
        blob(tx, ty - 16, 5, 5, 0xA0522Du);
        blob(tx - 4, ty - 20, 2, 2, 0xA0522Du);
        blob(tx + 4, ty - 20, 2, 2, 0xA0522Du);
        disc(tx, ty - 14, 2, 1, 0xD7A86Eu);
        rect(tx - 3, ty - 18, 2, 2, INK);
        rect(tx + 2, ty - 18, 2, 2, INK);
        peak(tx, ty - 28, 3, ty - 21, 0xE84393u);
        if (v->done >= 0)
            line(tx + 6, ty - 8, tx + 10, ty - 13 - (step & 1), INK);
    }
    if (v->done >= 0) /* balloons */
        for (i = 0; i < 3; i++) {
            int bx = 20 + i * 36, by = 70 - (v->done * 2 + i * 9) % 90;
            line(bx, by + 5, bx + ((step + i) & 1), by + 14, INK);
            blob(bx, by, 4, 5, FLAGS[(i + 1) & 3]);
        }
}

static void confetti(int done)
{
    int i;
    if (done < 0 || done >= 45)
        return;
    for (i = 0; i < 44; i++) {
        uint32_t h = hash(i, 23);
        int x = (int)(h % W) + ((done + i) % 6 < 3 ? 1 : -1), y = (int)((h >> 8) % 40u) - 40 + done * 3;
        rect(x, y, 3, 2 + (int)((h >> 20) & 1u), rh_note_colour((int)(h >> 16)));
    }
}

void rh_scene(int song, const rh_view_t *v, int step, uint32_t out[RH_SCENE_W * RH_SCENE_H])
{
    int sx = 60, sy = 30;
    px = out;
    switch (rh_song_def(song)->sound) {
    case RH_SOUND_XYLO: bakery(v, step, &sx, &sy); break;
    case RH_SOUND_KALIMBA: rain(v, step, &sx, &sy); break;
    case RH_SOUND_FLUTE: meadow(v, step, &sx, &sy); break;
    case RH_SOUND_MUSICBOX: night(v, step, &sx, &sy); break;
    case RH_SOUND_MARIMBA: river(v, step, &sx, &sy); break;
    case RH_SOUND_BELL: dawn(v, step, &sx, &sy); break;
    case RH_SOUND_TOYPIANO: bridge(v, step, &sx, &sy); break;
    case RH_SOUND_BANJO: farm(v, step, &sx, &sy); break;
    case RH_SOUND_HARP: spout(v, step, &sx, &sy); break;
    case RH_SOUND_SLEIGH: sleigh(v, step, &sx, &sy); break;
    default: party(v, step, &sx, &sy); break;
    }
    /* a burst of sparkles round whatever the note moved on, flying out */
    if (v->hit < 4 && v->done < 0) {
        static const int8_t DX[6] = {10, 5, -5, -10, -5, 5}, DY[6] = {0, -9, -9, 0, 7, 7};
        static const uint32_t C[3] = {WHITE, 0xFFF59Du, 0xFFB3D9u};
        int i, d = 4 + v->hit * 4, r = v->hit < 3 ? 3 : 2;
        for (i = 0; i < 6; i++)
            sparkle(sx + DX[i] * d / 10, sy + DY[i] * d / 10, r, C[i % 3]);
    }
    confetti(v->done);
}
