/* iScream as a mode: an ice cream synth, a sundae built from a flavor, a cone and toppings. Its flavors, voices and
 * effects are firmware/iscream/dsp's port of the desktop's IScreamCore (checked by firmware/iscream/tests/
 * run_parity.sh); this file is how the panel shows them and the sundae it draws. Included by app.c. */
#include "iscream.h"

enum { F_SCOOP, F_CONE, F_CREAM, F_FUDGE, F_CARAMEL, F_CHIPS, F_SPRINKLES, IS_NFOCUS };
static const char *const IS_FOCUS_TITLE[IS_NFOCUS] = {"Scoop", "Cone", "Whipped Cream", "Hot Fudge", "Caramel",
                                                      "Choc Chips", "Sprinkles"};
enum { IS_PCT = 1, IS_STYLE, IS_TOAST, IS_ROOM, IS_ONOFF };

/* The switch a focus has: the cone's, or a topping's; the scoop has none. */
static int is_switch(int f)
{
    if (f == F_SCOOP)
        return -1;
    return f == F_CONE ? ISC_CONE_ON : ISC_TOP(f - F_CREAM, 0);
}

static int is_on(int f) { return f == F_SCOOP || value(is_switch(f)) != 0; }

/* ------------------------------------------------------------------ the sundae on the screen */

#define CONE_H 50.f
#define SCOOP_R 25.f
#define SCOOP_STEP 22.f
#define APEX_X 120.f
#define APEX_Y 161.f

static float g_alpha = 1.f;             /* everything drawn is this opaque (the tray's switched-off icons) */

/* ------------------------------------------------------------------ colours and pixels */

static uint32_t mix_rgb(uint32_t a, uint32_t b, float t)
{
    uint32_t out = 0;
    int s;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    for (s = 0; s <= 16; s += 8) {
        float x = (float)((a >> s) & 0xFFu) * (1 - t) + (float)((b >> s) & 0xFFu) * t;
        out |= (uint32_t)(x + 0.5f) << s;
    }
    return out;
}

static uint32_t lighten(uint32_t c, float t) { return mix_rgb(c, 0xFFFFFFu, t); }

static void blend(int32_t x, int32_t y, uint32_t rgb, float a)
{
    uint16_t *p, c;
    uint32_t r, g, b, ia;
    if ((uint32_t)x >= 240u || y < band_y || y >= band_y + BAND_H)
        return;
    a *= g_alpha;
    if (a <= 0.f)
        return;
    p = &cv_px[(uint32_t)(y - band_y) * 240u + (uint32_t)x];
    if (a >= 0.999f) {
        *p = swap16(RGB((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu));
        return;
    }
    c = swap16(*p);
    ia = (uint32_t)(a * 256.f);
    r = ((c >> 11) & 31u) << 3, g = ((c >> 5) & 63u) << 2, b = (c & 31u) << 3;
    r = (r * (256u - ia) + ((rgb >> 16) & 0xFFu) * ia) >> 8;
    g = (g * (256u - ia) + ((rgb >> 8) & 0xFFu) * ia) >> 8;
    b = (b * (256u - ia) + (rgb & 0xFFu) * ia) >> 8;
    *p = swap16(RGB(r, g, b));
}

/* ------------------------------------------------------------------ clipping */

enum { CLIP_NONE, CLIP_CIRCLE, CLIP_POLY };
static int clip_mode;
static float clip_cx, clip_cy, clip_r;
static float clip_pts[24][2];
static int clip_n;

/* The polygon's extent on one row (it is convex where it is used). */
static int poly_extent(const float (*pts)[2], int n, float yc, float *l, float *r)
{
    int i, hit = 0;
    for (i = 0; i < n; i++) {
        const float *a = pts[i], *b = pts[(i + 1) % n];
        if ((a[1] <= yc && b[1] > yc) || (b[1] <= yc && a[1] > yc)) {
            float x = a[0] + (yc - a[1]) / (b[1] - a[1]) * (b[0] - a[0]);
            if (!hit || x < *l)
                *l = x;
            if (!hit || x > *r)
                *r = x;
            hit = 1;
        }
    }
    return hit;
}

static int clip_row(float yc, float *l, float *r)
{
    if (clip_mode == CLIP_CIRCLE) {
        float d = yc - clip_cy, w;
        if (d * d >= clip_r * clip_r)
            return 0;
        w = fm_sqrtf(clip_r * clip_r - d * d);
        *l = clip_cx - w, *r = clip_cx + w;
        return 1;
    }
    if (clip_mode == CLIP_POLY)
        return poly_extent((const float (*)[2])clip_pts, clip_n, yc, l, r);
    *l = -1e6f, *r = 1e6f;
    return 1;
}

/* One row from xa to xb, with the partly covered end pixels blended by how much they are covered. */
static void span(int32_t y, float xa, float xb, uint32_t rgb, float a)
{
    float cl, cr;
    int32_t x0, x1, x;
    if (y < band_y || y >= band_y + BAND_H || y < 0 || y >= 240)
        return;
    if (!clip_row((float)y + 0.5f, &cl, &cr))
        return;
    if (xa < cl)
        xa = cl;
    if (xb > cr)
        xb = cr;
    if (xa < 0)
        xa = 0;
    if (xb > 240.f)
        xb = 240.f;
    if (xb <= xa)
        return;
    x0 = (int32_t)xa;
    x1 = (int32_t)xb;
    if (x0 == x1) {
        blend(x0, y, rgb, a * (xb - xa));
        return;
    }
    blend(x0, y, rgb, a * ((float)(x0 + 1) - xa));
    for (x = x0 + 1; x < x1; x++)
        blend(x, y, rgb, a);
    blend(x1, y, rgb, a * (xb - (float)x1));
}

static int32_t row_lo(float y) { int32_t r = (int32_t)fm_floorf(y); return r < band_y ? band_y : r; }
static int32_t row_hi(float y) { int32_t r = (int32_t)fm_floorf(y) + 1; return r > band_y + BAND_H ? band_y + BAND_H : r; }

/* ------------------------------------------------------------------ shapes, in screen pixels */

static void ellipse(float cx, float cy, float rx, float ry, uint32_t rgb, float a)
{
    int32_t y;
    for (y = row_lo(cy - ry); y < row_hi(cy + ry); y++) {
        float d = ((float)y + 0.5f - cy) / ry, w;
        if (d * d >= 1.f)
            continue;
        w = rx * fm_sqrtf(1.f - d * d);
        span(y, cx - w, cx + w, rgb, a);
    }
}

/* An ellipse's outline, lw wide. */
static void is_ring(float cx, float cy, float rx, float ry, float lw, uint32_t rgb, float a)
{
    float ox = rx + lw / 2, oy = ry + lw / 2, ix = rx - lw / 2, iy = ry - lw / 2;
    int32_t y;
    for (y = row_lo(cy - oy); y < row_hi(cy + oy); y++) {
        float yc = (float)y + 0.5f - cy, d = yc / oy, wo, wi;
        if (d * d >= 1.f)
            continue;
        wo = ox * fm_sqrtf(1.f - d * d);
        d = iy > 0 ? yc / iy : 2.f;
        if (d * d >= 1.f) {
            span(y, cx - wo, cx + wo, rgb, a);
            continue;
        }
        wi = ix * fm_sqrtf(1.f - d * d);
        span(y, cx - wo, cx - wi, rgb, a);
        span(y, cx + wi, cx + wo, rgb, a);
    }
}

static void poly(const float (*pts)[2], int n, uint32_t rgb, float a)
{
    float lo = pts[0][1], hi = pts[0][1];
    int32_t y;
    int i;
    for (i = 1; i < n; i++) {
        lo = fm_minf(lo, pts[i][1]);
        hi = fm_maxf(hi, pts[i][1]);
    }
    for (y = row_lo(lo); y < row_hi(hi); y++) {
        float xs[24], yc = (float)y + 0.5f;
        int k = 0, j;
        for (i = 0; i < n && k < 24; i++) {
            const float *p = pts[i], *q = pts[(i + 1) % n];
            if ((p[1] <= yc && q[1] > yc) || (q[1] <= yc && p[1] > yc))
                xs[k++] = p[0] + (yc - p[1]) / (q[1] - p[1]) * (q[0] - p[0]);
        }
        for (i = 1; i < k; i++)                      /* a short insertion sort */
            for (j = i; j > 0 && xs[j] < xs[j - 1]; j--) {
                float t = xs[j];
                xs[j] = xs[j - 1];
                xs[j - 1] = t;
            }
        for (i = 0; i + 1 < k; i += 2)
            span(y, xs[i], xs[i + 1], rgb, a);
    }
}

static void line(float x0, float y0, float x1, float y1, float lw, uint32_t rgb, float a)
{
    float dx = x1 - x0, dy = y1 - y0, len = fm_sqrtf(dx * dx + dy * dy), nx, ny;
    float q[4][2];
    if (len < 1e-3f)
        return;
    nx = -dy / len * lw / 2, ny = dx / len * lw / 2;
    q[0][0] = x0 + nx, q[0][1] = y0 + ny;
    q[1][0] = x1 + nx, q[1][1] = y1 + ny;
    q[2][0] = x1 - nx, q[2][1] = y1 - ny;
    q[3][0] = x0 - nx, q[3][1] = y0 - ny;
    poly((const float (*)[2])q, 4, rgb, a);
}

/* A rounded line, ends included. */
static void stroke(float x0, float y0, float x1, float y1, float lw, uint32_t rgb, float a)
{
    line(x0, y0, x1, y1, lw, rgb, a);
    ellipse(x1, y1, lw / 2, lw / 2, rgb, a);
}

static int rrect_row(float x, float y, float w, float h, float r, float yc, float *l, float *rt)
{
    float inset = 0;
    if (yc < y || yc >= y + h)
        return 0;
    r = fm_minf(r, fm_minf(w, h) / 2);
    if (yc < y + r || yc > y + h - r) {
        float d = yc < y + r ? y + r - yc : yc - (y + h - r);
        inset = r - fm_sqrtf(fm_maxf(0, r * r - d * d));
    }
    *l = x + inset, *rt = x + w - inset;
    return 1;
}

static void rrect(float x, float y, float w, float h, float r, uint32_t rgb, float a)
{
    int32_t row;
    float l, rt;
    for (row = row_lo(y); row < row_hi(y + h); row++)
        if (rrect_row(x, y, w, h, r, (float)row + 0.5f, &l, &rt))
            span(row, l, rt, rgb, a);
}

static void rrect_stroke(float x, float y, float w, float h, float r, float lw, uint32_t rgb, float a)
{
    int32_t row;
    float hw = lw / 2;
    for (row = row_lo(y - hw); row < row_hi(y + h + hw); row++) {
        float yc = (float)row + 0.5f, ol, orr, il, ir;
        if (!rrect_row(x - hw, y - hw, w + lw, h + lw, r + hw, yc, &ol, &orr))
            continue;
        if (!rrect_row(x + hw, y + hw, w - lw, h - lw, fm_maxf(0, r - hw), yc, &il, &ir)) {
            span(row, ol, orr, rgb, a);
            continue;
        }
        span(row, ol, il, rgb, a);
        span(row, ir, orr, rgb, a);
    }
}

/* A ball lit from the upper left: three colours from the highlight out. */
static void shaded_ball(float cx, float cy, float r, uint32_t c0, uint32_t c1, uint32_t c2, float hx, float hy,
                        float reach)
{
    int32_t y, x;
    for (y = row_lo(cy - r); y < row_hi(cy + r); y++) {
        float d = ((float)y + 0.5f - cy) / r, w, l, rt, cl, cr;
        if (d * d >= 1.f)
            continue;
        w = r * fm_sqrtf(1.f - d * d);
        l = cx - w, rt = cx + w;
        if (!clip_row((float)y + 0.5f, &cl, &cr))
            continue;
        l = fm_maxf(fm_maxf(l, cl), 0.f);
        rt = fm_minf(fm_minf(rt, cr), 240.f);
        for (x = (int32_t)l; x <= (int32_t)rt && x < 240; x++) {
            float px = (float)x + 0.5f - hx, py = (float)y + 0.5f - hy;
            float t = fm_sqrtf(px * px + py * py) / reach;
            float cover = 1.f;
            uint32_t c = t < 0.5f ? mix_rgb(c0, c1, t * 2) : mix_rgb(c1, c2, (t - 0.5f) * 2);
            if (x == (int32_t)l)
                cover = (float)(x + 1) - l;
            else if (x == (int32_t)rt)
                cover = rt - (float)x;
            blend(x, y, c, cover);
        }
    }
}

static void is_text(float x, float y, const felucca_font_t *f, const char *s, uint32_t rgb, int centre)
{
    int32_t tx = (int32_t)x;
    if (centre)
        tx -= text_w(f, s) / 2;
    cv_text(tx, (int32_t)y - band_y, f, s, RGB((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu));
}

/* ------------------------------------------------------------------ the sundae's own coordinates */

/* The sundae is drawn around the point of the cone and scaled to fit, as on the desktop. */
static float T_ox, T_oy, T_sx, T_sy;
static float TX(float x) { return T_ox + x * T_sx; }
static float TY(float y) { return T_oy + y * T_sy; }

static void t_ellipse(float cx, float cy, float rx, float ry, uint32_t rgb, float a)
{
    ellipse(TX(cx), TY(cy), rx * T_sx, ry * T_sy, rgb, a);
}

static void t_poly(float (*pts)[2], int n, uint32_t rgb, float a)
{
    int i;
    for (i = 0; i < n; i++)
        pts[i][0] = TX(pts[i][0]), pts[i][1] = TY(pts[i][1]);
    poly((const float (*)[2])pts, n, rgb, a);
}

static void t_line(float x0, float y0, float x1, float y1, float lw, uint32_t rgb, float a)
{
    line(TX(x0), TY(y0), TX(x1), TY(y1), lw * T_sx, rgb, a);
}

static void t_stroke(float x0, float y0, float x1, float y1, float lw, uint32_t rgb, float a)
{
    stroke(TX(x0), TY(y0), TX(x1), TY(y1), lw * T_sx, rgb, a);
}

static void t_rrect(float x, float y, float w, float h, float r, uint32_t rgb, float a)
{
    rrect(TX(x), TY(y), w * T_sx, h * T_sy, r * T_sx, rgb, a);
}

/* A rotated rounded bar (sprinkles, chunks), as a polygon. */
static void t_bar(float cx, float cy, float w, float h, float angle, uint32_t rgb, float a)
{
    float c = fm_cosf(angle), s = fm_sinf(angle), q[4][2];
    float xs[4] = {-w / 2, w / 2, w / 2, -w / 2}, ys[4] = {-h / 2, -h / 2, h / 2, h / 2};
    int i;
    for (i = 0; i < 4; i++)
        q[i][0] = cx + xs[i] * c - ys[i] * s, q[i][1] = cy + xs[i] * s + ys[i] * c;
    t_poly(q, 4, rgb, a);
}

/* Points along a quadratic curve, appended to pts from index n; returns the new count. */
static int quad(float (*pts)[2], int n, float x0, float y0, float cx, float cy, float x1, float y1, int steps)
{
    int i;
    for (i = 1; i <= steps && n < 24; i++) {
        float t = (float)i / (float)steps, u = 1 - t;
        pts[n][0] = u * u * x0 + 2 * u * t * cx + t * t * x1;
        pts[n][1] = u * u * y0 + 2 * u * t * cy + t * t * y1;
        n++;
    }
    return n;
}

/* A small random number generator, so each flavor's chips and specks are always in the same places. */
static uint32_t scatter_s;
static void scatter_seed(uint32_t s) { scatter_s = s * 2654435761u + 12345u; }
static float scatter(float lo, float hi)
{
    scatter_s = scatter_s * 1664525u + 1013904223u;
    return lo + (float)(scatter_s >> 8) / 16777216.f * (hi - lo);
}

static float center_y(int i) { return -(CONE_H + 8) - (float)i * SCOOP_STEP; }
static float top_y(int n) { return center_y(n - 1) - SCOOP_R; }

static float sundae_scale(int n, int cream)
{
    float h = -top_y(n) + (cream ? 36.f : 4.f);
    return fm_minf(1.3f, 122.f / h);
}

/* ------------------------------------------------------------------ the parts of a sundae */

static const uint32_t SPRINKLE_COLORS[6] = {0xFF3D8Bu, 0x37B6FFu, 0xFFD23Du, 0x56D66Bu, 0xFF8A2Bu, 0xA066FFu};

static int cone_outline(int style, float (*p)[2])
{
    int n = 0;
    if (style == 2) {
        p[n][0] = -32, p[n++][1] = -CONE_H;
        p[n][0] = 32, p[n++][1] = -CONE_H;
        p[n][0] = 19, p[n++][1] = -3;
        p[n][0] = 14, p[n++][1] = 0;
        p[n][0] = -14, p[n++][1] = 0;
        p[n][0] = -19, p[n++][1] = -3;
    } else {
        p[n][0] = -27, p[n++][1] = -CONE_H;
        p[n][0] = 27, p[n++][1] = -CONE_H;
        p[n][0] = 2.5f, p[n++][1] = -1;
        p[n][0] = 0, p[n++][1] = 1.5f;
        p[n][0] = -2.5f, p[n++][1] = -1;
    }
    return n;
}

static void outline_stroke(float (*p)[2], int n, float lw, uint32_t rgb, float a, int dashed)
{
    int i;
    for (i = 0; i < n; i++) {
        float *s = p[i], *e = p[(i + 1) % n];
        if (!dashed) {
            stroke(s[0], s[1], e[0], e[1], lw, rgb, a);
            continue;
        }
        {
            float dx = e[0] - s[0], dy = e[1] - s[1], len = fm_sqrtf(dx * dx + dy * dy), d;
            for (d = 0; d < len; d += 6)
                line(s[0] + dx * d / len, s[1] + dy * d / len, s[0] + dx * fm_minf(d + 3, len) / len,
                     s[1] + dy * fm_minf(d + 3, len) / len, lw, rgb, a);
        }
    }
}

static void draw_cone(void)
{
    int style = value(ISC_CONE_STYLE), n, i;
    float o[24][2], toast = (float)value(ISC_TOAST) / 107.f, crunch = (float)value(ISC_CRUNCH) / 100.f;
    int32_t y;
    n = cone_outline(style, o);
    for (i = 0; i < n; i++)
        o[i][0] = TX(o[i][0]), o[i][1] = TY(o[i][1]);
    if (!value(ISC_CONE_ON)) {
        /* No cone: the shape is left as a faint dotted outline. */
        outline_stroke(o, n, 1.6f, 0xFFFFFFu, 0.7f, 1);
        return;
    }
    isc_memcpy(clip_pts, o, sizeof o);
    clip_n = n;
    clip_mode = CLIP_POLY;
    if (style == 2) {
        /* A paper cup in pink and white stripes. */
        float x;
        rrect(TX(-34), TY(-CONE_H), 68 * T_sx, CONE_H * T_sy, 0, 0xFFFFFFu, 1);
        for (x = -34; x < 34; x += 16) {
            float q[4][2] = {{x, -CONE_H}, {x + 8, -CONE_H}, {x * 0.58f + 4.6f, 0}, {x * 0.58f, 0}};
            t_poly(q, 4, mix_rgb(0xFF8FC8u, 0xD2227Fu, toast), 1);
        }
        t_rrect(-34, -CONE_H, 68, 6, 0, 0xD2227Fu, 0.85f);
    } else {
        uint32_t pale = style == 1 ? 0xF6DFAEu : 0xEFC98Au, deep = style == 1 ? 0xD9A35Au : 0xC47F35u;
        uint32_t ink = mix_rgb(deep, 0x6B3A12u, 0.5f);
        float ia = 0.35f + 0.5f * crunch, lw = 0.9f + crunch * 1.4f;
        for (y = row_lo(TY(-CONE_H)); y < row_hi(TY(0)); y++) {
            float f = ((float)y + 0.5f - TY(-CONE_H)) / (CONE_H * T_sy);
            span(y, 0, 240, mix_rgb(mix_rgb(pale, deep, toast), mix_rgb(pale, deep, fm_minf(1, toast + 0.25f)), f), 1);
        }
        if (style == 0) {
            float x;
            for (x = -60; x < 60; x += 11) {
                t_line(x, -CONE_H, x + 40, 0, lw, ink, ia);
                t_line(x + 40, -CONE_H, x, 0, lw, ink, ia);
            }
        } else {
            float x;
            for (x = -30; x <= 30; x += 6)
                t_line(x, -CONE_H, x * 0.08f, 0, lw * 0.8f, ink, ia);
            t_rrect(-30, -CONE_H, 60, 7, 0, mix_rgb(deep, 0x8A5420u, 0.4f), 0.8f);
        }
    }
    clip_mode = CLIP_NONE;
    outline_stroke(o, n, 1, 0x8A5A2Au, 0.55f, 0);
}

static void draw_bits(float cx, float cy, float r, const isc_flavor_t *info, uint32_t seed)
{
    int i;
    scatter_seed(seed);
    clip_mode = CLIP_CIRCLE;
    clip_cx = TX(cx), clip_cy = TY(cy), clip_r = r * T_sx;
    switch (info->pattern) {
    case 1:
        for (i = 0; i < 18; i++) {
            float x = scatter(-0.85f, 0.85f) * r, y = scatter(-0.85f, 0.85f) * r;
            t_ellipse(cx + x, cy + y, 1, 0.8f, info->bits, 1);
        }
        break;
    case 2:
        for (i = 0; i < 9; i++) {
            float x = scatter(-0.8f, 0.8f) * r, y = scatter(-0.8f, 0.8f) * r;
            t_bar(cx + x, cy + y, scatter(4, 7), scatter(3, 5), scatter(0, 3), info->bits, 1);
        }
        break;
    case 3:
        for (i = 0; i < 3; i++) {
            float rad = r * (0.35f + 0.28f * (float)i), a0 = (200.f + 25.f * (float)i) * FM_PI / 180.f, a;
            float px = cx - r * 0.1f + rad * fm_cosf(a0), py = cy + r * 0.1f + rad * fm_sinf(a0);
            for (a = a0 + 0.15f; a < a0 + 180.f * FM_PI / 180.f; a += 0.15f) {
                float nx = cx - r * 0.1f + rad * fm_cosf(a), ny = cy + r * 0.1f + rad * fm_sinf(a);
                t_stroke(px, py, nx, ny, 3, info->bits, 0.85f);
                px = nx, py = ny;
            }
        }
        break;
    }
    clip_mode = CLIP_NONE;
}

static void draw_scoop(float cy, float scale_r, const isc_flavor_t *info, uint32_t seed)
{
    float r = scale_r;
    int j;
    /* The ruffled foot under the ball. */
    uint32_t foot = mix_rgb(lighten(info->color, 0.08f), mix_rgb(info->color, info->accent, 0.55f), 0.5f);
    t_rrect(-r * 1.12f, cy + r * 0.55f, r * 2.24f, r * 0.34f, r * 0.17f, foot, 1);
    for (j = 0; j < 9; j++)
        t_ellipse(-r + 2 * r * (float)j / 8.f, cy + r * 0.86f, r * 0.17f, r * 0.17f, foot, 1);
    /* The ball. */
    shaded_ball(TX(0), TY(cy), r * T_sx, lighten(info->color, 0.3f), info->color, mix_rgb(info->color, info->accent, 0.75f),
                TX(-r * 0.32f), TY(cy - r * 0.38f), r * 1.5f * T_sx);
    draw_bits(0, cy, r, info, seed);
    is_ring(TX(0), TY(cy), r * T_sx, r * T_sy, 0.8f, info->accent, 0.4f);
    t_ellipse(-r * 0.345f, cy - r * 0.57f, r * 0.275f, r * 0.15f, 0xFFFFFFu, 0.4f);
}

static void draw_melt(float melt, float t, const isc_flavor_t *info)
{
    static const float XS[4] = {-0.8f, -0.3f, 0.35f, 0.82f};
    float y0 = center_y(0) + SCOOP_R * 0.9f;
    int i;
    for (i = 0; i < 4; i++) {
        float len = (4 + 15 * melt) * (0.7f + 0.3f * fm_sinf(t * 0.8f + (float)i * 1.9f)), x = XS[i] * SCOOP_R;
        t_rrect(x - 2.4f, y0 - 2, 4.8f, len + 2, 2.4f, info->color, 1);
        t_ellipse(x, y0 + len, 3, 3, info->color, 1);
    }
}

static void draw_fudge(float cy, float amount)
{
    static const float LENS[5] = {0.9f, 0.45f, 1.1f, 0.6f, 0.8f};
    float r = SCOOP_R, edge = cy - r * 0.45f + amount * r * 0.75f, p[24][2], x = r + 2;
    int n = 0, up = 1, i;
    p[n][0] = -r - 2, p[n++][1] = cy - r - 2;
    p[n][0] = r + 2, p[n++][1] = cy - r - 2;
    p[n][0] = r + 2, p[n++][1] = edge;
    while (x > -r - 2 && n < 22) {
        float nx = x - r * 0.45f;
        n = quad(p, n, x, edge, (x + nx) / 2, edge + (up ? 6.f : -2.f), nx, edge, 3);
        x = nx;
        up = !up;
    }
    clip_mode = CLIP_CIRCLE;
    clip_cx = TX(0), clip_cy = TY(cy), clip_r = r * T_sx;
    t_poly(p, n, 0x56301Cu, 1);
    t_ellipse(-r * 0.3f, cy - r * 0.69f, r * 0.25f, r * 0.11f, 0xFFFFFFu, 0.25f);
    clip_mode = CLIP_NONE;
    /* Drips run over the edge. */
    for (i = 0; i < 5; i++) {
        float dx = -r * 0.78f + (float)i * r * 0.39f, len = (4 + amount * r * 0.7f) * LENS[i], y = edge + 2;
        t_rrect(dx - 2.4f, y - 3, 4.8f, len + 3, 2.4f, 0x4A2515u, 1);
        t_ellipse(dx, y + len, 3, 3, 0x4A2515u, 1);
    }
}

static void draw_caramel(float cy, float amount)
{
    float r = SCOOP_R, w = 1.6f + amount * 2.6f;
    int k, j;
    for (k = 0; k < 2; k++) {
        float y0 = cy - r * 0.1f + (float)k * r * 0.55f, px = -r * 0.9f, py = y0;
        for (j = 1; j <= 6; j++) {
            float x = -r * 0.9f + (float)j * r * 0.3f, y = y0 + (j % 2 == 0 ? -5.f : 5.f);
            t_stroke(px, py, x, y, w, 0xD68A1Fu, 1);
            t_stroke(px, py, x, y, w * 0.35f, 0xFFC866u, 0.8f);
            px = x, py = y;
        }
    }
}

static void draw_choc_chips(int n, float amount)
{
    int count = 5 + (int)(amount * 14), i;
    float r = SCOOP_R, top = center_y(n - 1);
    scatter_seed(99);
    for (i = 0; i < count; i++) {
        float x = scatter(-r * 0.85f, r * 0.85f), y = top + scatter(-r * 0.75f, r * 1.9f), a = scatter(0, 6);
        float c = fm_cosf(a), s = fm_sinf(a);
        float tri[3][2] = {{x + -2.6f * c - 2 * s, y + -2.6f * s + 2 * c},
                           {x + 2.6f * c - 2 * s, y + 2.6f * s + 2 * c},
                           {x + 2.8f * s, y - 2.8f * c}};
        if (y >= center_y(0) + r * 0.6f)
            continue;
        t_poly(tri, 3, 0x4A2A18u, 1);
    }
}

/* A dollop of whipped cream with a curled tip; bx, by is the middle of its foot. */
static void draw_cream(float bx, float by, float k)
{
    static const float TIERS[3][3] = {{46, 15, 0}, {35, 14, -9}, {24, 13, -17}};
    float tip[24][2];
    int i, j, n = 0;
    for (i = 0; i < 3; i++) {
        float w = TIERS[i][0] * k, h = TIERS[i][1] * k, y = by + TIERS[i][2] * k - h / 2;
        t_rrect(bx - w / 2, y, w, h, h / 2, 0xF4F2FAu, 1);
        t_rrect(bx - w / 2, y + h * 0.55f, w, h * 0.45f, h / 4, 0xE8E4F4u, 0.8f);
    }
    tip[n][0] = bx - 8 * k, tip[n++][1] = by - 22 * k;
    n = quad(tip, n, bx - 8 * k, by - 22 * k, bx - 6 * k, by - 32 * k, bx + 2 * k, by - 36 * k, 5);
    n = quad(tip, n, bx + 2 * k, by - 36 * k, bx + 6 * k, by - 30 * k, bx + 8 * k, by - 22 * k, 5);
    t_poly(tip, n, 0xFFFFFFu, 1);
    for (i = 0; i < 3; i++)
        for (j = -1; j <= 1; j++) {
            float x = bx + (float)j * TIERS[i][0] * k * 0.22f, yc = by + TIERS[i][2] * k, h = TIERS[i][1] * k;
            t_line(x, yc - h * 0.35f, x + 1.5f, yc + h * 0.35f, 0.7f, 0xB9B0D0u, 0.55f);
        }
}

static void draw_cherry(float x, float y)
{
    float stem[24][2];
    int n = quad(stem, 0, x, y - 3, x + 1, y - 9, x + 7, y - 12, 6), i;
    float px = x, py = y - 3;
    for (i = 0; i < n; i++) {
        t_stroke(px, py, stem[i][0], stem[i][1], 1.4f, 0x3F7A2Eu, 1);
        px = stem[i][0], py = stem[i][1];
    }
    shaded_ball(TX(x), TY(y + 0.5f), 5.5f * T_sx, 0xFF6A7Au, 0xE03050u, 0xC8102Eu, TX(x - 1.5f), TY(y - 2), 8 * T_sx);
    t_ellipse(x - 1.5f, y - 2.4f, 1.5f, 1, 0xFFFFFFu, 0.7f);
}

static void draw_sprinkles(float center, int cream, float amount, float t)
{
    int count = 6 + (int)(amount * 26), i;
    float r = SCOOP_R, up = cream ? 22.f : 0.f;
    scatter_seed(1234);
    for (i = 0; i < count; i++) {
        float x = scatter(-r * 0.9f, r * 0.9f), y = center + scatter(-r * 0.75f - up, r * 0.55f), rot = scatter(0, 3.14f);
        float tw = 0.75f + 0.25f * fm_sinf(t * 3 + (float)i * 2.3f);
        t_bar(x, y, 5.6f, 1.8f, rot, SPRINKLE_COLORS[i % 6], tw);
    }
}

static void draw_fizz(int n, float amount, float t)
{
    int count = 2 + (int)(amount * 5), i;
    for (i = 0; i < count; i++) {
        float ph = t * (0.35f + 0.05f * (float)i) + (float)i * 0.37f, x, y, r;
        ph -= fm_floorf(ph);
        x = fm_sinf((float)i * 2.1f) * 26;
        y = top_y(n) - 6 - ph * 26;
        r = 1.4f + (float)(i % 3) * 0.7f;
        is_ring(TX(x), TY(y), r * T_sx, r * T_sy, 1, 0xFFFFFFu, 0.85f * (1 - ph));
    }
}

static void dashed_rrect(float x, float y, float w, float h, float t)
{
    float a = 0.55f + 0.45f * fm_sinf(t * 5), d;
    float sx = TX(x), sy = TY(y), sw = w * T_sx, sh = h * T_sy, r = 10 * T_sx;
    for (d = sx + r; d < sx + sw - r; d += 9) {
        line(d, sy, fm_minf(d + 5, sx + sw - r), sy, 1.8f, 0xFFFFFFu, a);
        line(d, sy + sh, fm_minf(d + 5, sx + sw - r), sy + sh, 1.8f, 0xFFFFFFu, a);
    }
    for (d = sy + r; d < sy + sh - r; d += 9) {
        line(sx, d, sx, fm_minf(d + 5, sy + sh - r), 1.8f, 0xFFFFFFu, a);
        line(sx + sw, d, sx + sw, fm_minf(d + 5, sy + sh - r), 1.8f, 0xFFFFFFu, a);
    }
}

static void draw_sundae(float t, float peak)
{
    const isc_flavor_t *info = isc_flavor(item());
    int n = clamp(value(ISC_SCOOPS), 1, 3), cream = value(ISC_TOP(ISC_WHIPPED_CREAM, 0)) != 0, i;
    float scale = sundae_scale(n, cream), loud = fm_minf(1, peak * 4), wob = loud * 0.035f * fm_sinf(t * 18);
    float melt = (float)(value(ISC_MELT) - 50) / 50.f, top = center_y(n - 1), k;
    T_ox = APEX_X, T_oy = APEX_Y, T_sx = scale * (1 - wob * 0.6f), T_sy = scale * (1 + wob);

    t_ellipse(0, 2, 26, 4, 0xB23A7Eu, 0.18f);           /* the ground shadow */
    draw_cone();
    for (i = 0; i < n; i++)
        draw_scoop(center_y(i), SCOOP_R, info, (uint32_t)(item() * 31 + 5 + i * 7));
    if (melt > 0.05f)
        draw_melt(melt, t, info);
    if (value(ISC_TOP(ISC_HOT_FUDGE, 0)))
        draw_fudge(top, (float)value(ISC_TOP(ISC_HOT_FUDGE, 1)) / 100.f);
    if (value(ISC_TOP(ISC_CARAMEL, 0)))
        draw_caramel(top, (float)value(ISC_TOP(ISC_CARAMEL, 1)) / 100.f);
    if (value(ISC_TOP(ISC_CHOC_CHIPS, 0)))
        draw_choc_chips(n, (float)value(ISC_TOP(ISC_CHOC_CHIPS, 1)) / 100.f);
    k = 0.8f + (float)value(ISC_TOP(ISC_WHIPPED_CREAM, 1)) / 100.f * 0.35f;
    if (cream)
        draw_cream(0, top_y(n) + 5, k);
    if (value(ISC_TOP(ISC_SPRINKLES, 0)))
        draw_sprinkles(top, cream, (float)value(ISC_TOP(ISC_SPRINKLES, 1)) / 100.f, t);
    if (cream)
        draw_cherry(1, top_y(n) - 33 * k + 2);
    if (value(ISC_FIZZ) > 0)
        draw_fizz(n, (float)value(ISC_FIZZ) / 100.f, t);
    /* What the knobs are on. */
    if (page == F_CONE)
        dashed_rrect(-36, -CONE_H - 3, 72, CONE_H + 6, t);
    else if (page == F_SCOOP)
        dashed_rrect(-SCOOP_R - 8, top_y(n) - 4, SCOOP_R * 2 + 16, center_y(0) + SCOOP_R * 1.1f - top_y(n) + 8, t);
    else {
        float extra = page == F_CREAM ? 30.f : 0.f;
        dashed_rrect(-SCOOP_R - 8, top - SCOOP_R - 4 - extra, SCOOP_R * 2 + 16, SCOOP_R * 2 + 8 + extra, t);
    }
}

/* ------------------------------------------------------------------ the frame around it */

static void draw_backdrop(float t)
{
    int32_t y;
    int i;
    for (y = band_y; y < band_y + BAND_H; y++)
        span(y, 0, 240, mix_rgb(0xFFE6F3u, 0xFFB4DCu, (float)y / 240.f), 1);
    scatter_seed(7);
    for (i = 0; i < 26; i++) {
        float x = scatter(0, 240), yy = scatter(36, 162), r = scatter(2, 7);
        ellipse(x, yy, r, r, 0xFFFFFFu, 0.22f);
    }
    for (i = 0; i < 5; i++) {                           /* a few sparkles that twinkle */
        float x = scatter(10, 230), yy = scatter(40, 150), a = 0.35f + 0.35f * fm_sinf(t * 2.2f + (float)i * 1.7f);
        float s[4][2] = {{x, yy - 4}, {x + 1.2f, yy}, {x, yy + 4}, {x - 1.2f, yy}};
        float s2[4][2] = {{x - 4, yy}, {x, yy - 1.2f}, {x + 4, yy}, {x, yy + 1.2f}};
        poly((const float (*)[2])s, 4, 0xFFFFFFu, a);
        poly((const float (*)[2])s2, 4, 0xFFFFFFu, a);
    }
}

static void is_title(void)
{
    const isc_flavor_t *info = isc_flavor(item());
    rrect(0, 0, 240, 34, 0, 0xFF5FAEu, 0.18f);
    is_text(9, 2, &FONT_M, info->title, 0x8A1A57u, 0);
    is_text(10, 21, &FONT_T, info->blurb, 0xB23A7Eu, 0);
    rrect(0, 33, 240, 1, 0, 0xFF5FAEu, 0.35f);
}

static void is_badge(void)
{
    const char *label = IS_FOCUS_TITLE[page];
    float w = (float)text_w(&FONT_XS, label) + 26, x = 236 - w;
    rrect(x, 42, w, 20, 10, 0xFFFFFFu, 0.85f);
    rrect_stroke(x, 42, w, 20, 10, 1, 0xFF5FAEu, 0.7f);
    is_text(x + 18, 44, &FONT_XS, label, 0x8A1A57u, 0);
    ellipse(x + 10, 52, 4, 4, is_on(page) ? 0xFF2D8Au : 0xC9A3B8u, 1);
}

static void draw_icon(int f, float cx, float cy, const isc_flavor_t *info)
{
    T_ox = cx, T_oy = cy;
    switch (f) {
    case F_SCOOP:
        T_sx = T_sy = 0.5f;
        draw_scoop(0, SCOOP_R, info, 3);
        break;
    case F_CONE: {
        float o[24][2];
        int n = cone_outline(0, o), i;
        T_oy = cy + 9, T_sx = T_sy = 0.42f;
        for (i = 0; i < n; i++)
            o[i][0] = TX(o[i][0]), o[i][1] = TY(o[i][1]);
        poly((const float (*)[2])o, n, 0xE3B26Bu, 1);
        outline_stroke(o, n, 0.9f, 0x8A5A2Au, 1, 0);
        for (i = -20; i <= 20; i += 14)
            t_line((float)i, -50, (float)i * 0.1f, -2, 1.6f, 0x8A5A2Au, 0.7f);
        break;
    }
    case F_CREAM:
        T_oy = cy + 9, T_sx = T_sy = 0.5f;
        draw_cream(0, 0, 1);
        break;
    case F_FUDGE: {
        float b[24][2];
        int n = 0;
        T_sx = T_sy = 1;
        b[n][0] = -10, b[n++][1] = -6;
        n = quad(b, n, -10, -6, 0, -14, 10, -6, 4);
        b[n][0] = 10, b[n++][1] = 2;
        n = quad(b, n, 10, 2, 9, 8, 7, 8, 2);
        n = quad(b, n, 7, 8, 5, 8, 4, 2, 2);
        b[n][0] = -3, b[n++][1] = 2;
        n = quad(b, n, -3, 2, -3, 11, -6, 10, 2);
        n = quad(b, n, -6, 10, -9, 10, -9, 2, 2);
        t_poly(b, n, 0x4A2515u, 1);
        break;
    }
    case F_CARAMEL: {
        int j;
        float px = -10, py = 3;
        T_sx = T_sy = 1;
        for (j = 1; j <= 5; j++) {
            float x = -10 + (float)j * 4, y = j % 2 == 0 ? 3.f : -5.f;
            t_stroke(px, py, x, y, 3.4f, 0xD68A1Fu, 1);
            px = x, py = y;
        }
        break;
    }
    case F_CHIPS: {
        static const float C[4][3] = {{-6, -2, 0.4f}, {5, -5, 1.6f}, {3, 5, 3.0f}, {-5, 6, 4.5f}};
        int j;
        T_sx = T_sy = 1;
        for (j = 0; j < 4; j++) {
            float c = fm_cosf(C[j][2]), s = fm_sinf(C[j][2]), x = C[j][0], y = C[j][1];
            float tri[3][2] = {{x - 3.6f * c - 3 * s, y - 3.6f * s + 3 * c},
                               {x + 3.6f * c - 3 * s, y + 3.6f * s + 3 * c},
                               {x + 4 * s, y - 4 * c}};
            t_poly(tri, 3, 0x4A2A18u, 1);
        }
        break;
    }
    case F_SPRINKLES: {
        static const float P[6][2] = {{-7, -4}, {4, -6}, {8, 1}, {-2, 3}, {-8, 7}, {3, 8}};
        int j;
        T_sx = T_sy = 1;
        for (j = 0; j < 6; j++)
            t_bar(P[j][0], P[j][1], 6.8f, 2.2f, (float)j * 1.1f, SPRINKLE_COLORS[j], 1);
        break;
    }
    }
}

static void is_tray(void)
{
    const isc_flavor_t *info = isc_flavor(item());
    int i;
    for (i = 0; i < IS_NFOCUS; i++) {
        float x = 3 + (float)i * 34, y = 167;
        int focused = page == i;
        rrect(x, y, 31, 30, 9, 0xFFFFFFu, focused ? 1.f : 0.45f);
        rrect_stroke(x, y, 31, 30, 9, focused ? 2.f : 1.f, focused ? 0xFF2D8Au : 0xFF5FAEu, focused ? 1.f : 0.5f);
        g_alpha = is_on(i) ? 1.f : 0.35f;
        draw_icon(i, x + 15.5f, y + 16, info);
        g_alpha = 1.f;
    }
}

static void is_chips(void)
{
    int on = is_on(page), i;
    for (i = 0; i < 4; i++) {
        const md_param_t *p = &M->params[page][i];
        float x = 5 + (float)i * 59, y = 201;
        int v = value(p->field), dim = !on && p->fmt != IS_ONOFF;
        char t[16];
        rrect(x, y, 55, 36, 9, 0xFFFFFFu, dim ? 0.45f : 0.9f);
        rrect_stroke(x, y, 55, 36, 9, 1, 0xFF5FAEu, 0.7f);
        is_text(x + 27.5f, y + 1, &FONT_T, p->name, 0xB23A7Eu, 1);
        format(p->fmt, v, t);
        if (p->fmt == IS_ONOFF) {
            is_text(x + 27.5f, y + 15, &FONT_B, t, v ? 0xFF2D8Au : 0xB9A0B0u, 1);
        } else {
            int lo, hi;
            float f;
            isc_range(p->field, &lo, &hi);
            f = (float)(v - lo) / (float)(hi - lo > 0 ? hi - lo : 1);
            is_text(x + 27.5f, y + 12, &FONT_B, t, 0x8A1A57u, 1);
            rrect(x + 7, y + 29.5f, 41, 3.5f, 1.75f, 0xFF5FAEu, 0.22f);
            rrect(x + 7, y + 29.5f, fm_maxf(3.5f, 41 * f), 3.5f, 1.75f, 0xFF2D8Au, 1);
        }
    }
}

static void is_pill(void)
{
    rrect(14, 204, 212, 28, 14, 0xFF2D8Au, 1);
    rrect_stroke(14, 204, 212, 28, 14, 1.5f, 0xFFFFFFu, 0.7f);
    is_text(120, 210, &FONT_B, ui.pill, 0xFFFFFFu, 1);
}

static void is_draw(uint32_t now)
{
    float t = (float)now / 1000.f;
    draw_backdrop(t);
    is_title();
    draw_battery(207, 9, 1, 0x8A1A57u);
    draw_sundae(t, peak_level);
    is_badge();
    is_tray();
    if (ui.pill[0])
        is_pill();
    else
        is_chips();
}

/* ------------------------------------------------------------------ the panel */

static void is_pattern(const int32_t *s, md_pattern_t *p)
{
    (void)s;
    isc_memset(p, 0, sizeof *p);
    p->tempo = 120;
}

static int is_key_notes(const int32_t *s, int note, int left, int out[8])
{
    (void)s, (void)left;
    out[0] = note;
    return 1;
}

static int is_mono(const int32_t *s) { return isc_flavor(s[ISC_FLAVOR])->mono; }
static int is_releases(int field) { (void)field; return 0; }
static const char *is_item_name(int i) { return isc_flavor(i)->title; }
static void is_press(uint32_t b) { (void)b; }

static void is_format(int fmt, int v, char *out)
{
    static const char *const STYLES[3] = {"Waffle", "Sugar", "Cup"};
    static const char *const ROOMS[3] = {"Room", "Hall", "Plate"};
    switch (fmt) {
    case IS_PCT:
        int_text(v, out);
        text_cat(out, "%");
        break;
    case IS_STYLE: text_cat(out, STYLES[clamp(v, 0, 2)]); break;
    case IS_ROOM: text_cat(out, ROOMS[clamp(v, 0, 2)]); break;
    case IS_ONOFF: text_cat(out, v ? "On" : "Off"); break;
    case IS_TOAST: {
        /* the cutoff in Hz, as the cone's toast reads it */
        int hz = (int)(100.f * fm_powf(200.f, (float)v / 107.f) + 0.5f);
        if (hz >= 1000) {
            char tenths[4] = {'.', 0, 'k', 0};
            tenths[1] = (char)('0' + (hz % 1000) / 100);
            int_text(hz / 1000, out);
            text_cat(out, tenths);
        } else {
            int_text(hz, out);
        }
        break;
    }
    }
}

/* The button of the focus already showing switches it on or off. */
static void is_repress(int f)
{
    int field = is_switch(f), on;
    if (field < 0)
        return;
    on = value(field) == 0;
    set_field(field, on);
    show_pill(IS_FOCUS_TITLE[f], on ? "On" : "Off");
}

/* The on/off chip turns its switch; any other knob on something switched off switches it on, so there is no menu
 * to dive for. */
static int is_knob(int k, int delta)
{
    const md_param_t *p = &M->params[page][k];
    int sw;
    if (p->fmt == IS_ONOFF) {
        int want = delta > 0;
        if (value(p->field) != want) {
            set_field(p->field, want);
            show_pill(IS_FOCUS_TITLE[page], want ? "On" : "Off");
        }
        return 1;
    }
    sw = is_switch(page);
    if (sw >= 0 && value(sw) == 0)
        set_field(sw, 1);
    return 0;
}

/* The toppings and cone that are on light dimly. */
static uint32_t is_dim(void)
{
    static const uint8_t BUTTON[IS_NFOCUS] = {B_HOME, B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO};
    uint32_t dim = 0;
    int f;
    for (f = F_CONE; f < IS_NFOCUS; f++)
        if (f != page && is_on(f))
            dim |= 1u << BUTTON[f];
    return dim;
}

static int is_expect(const char *what, int *got)
{
    if (isc_streq(what, "flavor"))
        *got = item();
    else if (isc_streq(what, "focus"))
        *got = page;
    else if (isc_streq(what, "scoops"))
        *got = value(ISC_SCOOPS);
    else if (isc_streq(what, "sweet"))
        *got = value(ISC_SWEET);
    else if (isc_streq(what, "cone"))
        *got = value(ISC_CONE_ON);
    else
        return 0;
    return 1;
}

#define IS_TOP(t, a, b, c, n, fmt3)                                                                            \
    {{a, ISC_TOP(t, 1), IS_PCT}, {b, ISC_TOP(t, 2), IS_PCT}, {c, ISC_TOP(t, 3), fmt3}, {n, ISC_TOP(t, 0), IS_ONOFF}}

static const md_mode_t MODE_ISCREAM = {
    .name = "iScream",
    .reset_words = "a fresh scoop",
    .reset_pill = "Fresh scoop",
    .obj = OBJ_ISCREAM,
    .magic = 0x31435349u, /* "ISC1" */
    .version = 1,
    .fields = ISC_FIELDS,
    .npages = IS_NFOCUS,
    .page_title = {"Scoop", "Cone", "Cream", "Fudge", "Caramel", "Chips", "Sprinkles"},
    .params = {{{"Sweet", ISC_SWEET, FMT_NUM}, {"Melt", ISC_MELT, FMT_NUM}, {"Fizz", ISC_FIZZ, FMT_NUM},
                {"Scoops", ISC_SCOOPS, FMT_NUM}},
               {{"Style", ISC_CONE_STYLE, IS_STYLE}, {"Toast", ISC_TOAST, IS_TOAST}, {"Crunch", ISC_CRUNCH, FMT_NUM},
                {"Cone", ISC_CONE_ON, IS_ONOFF}},
               IS_TOP(ISC_WHIPPED_CREAM, "Mix", "Rate", "Depth", "Cream", IS_PCT),
               IS_TOP(ISC_HOT_FUDGE, "Gain", "Tone", "Level", "Fudge", IS_PCT),
               IS_TOP(ISC_CARAMEL, "Drizzle", "Rate", "Depth", "Caramel", IS_PCT),
               IS_TOP(ISC_CHOC_CHIPS, "Mix", "Echo", "Speed", "Chips", IS_PCT),
               IS_TOP(ISC_SPRINKLES, "Mix", "Decay", "Room", "Sprinkles", IS_ROOM)},
    .algo_field = ISC_SCOOPS,
    .algo_name = "Scoops",
    .algo_fmt = FMT_NUM,
    .count = isc_flavor_count,
    .defaults = isc_default_sundae,
    .range = isc_range,
    .clamp = isc_clamp,
    .voice = isc_voice,
    .fx = isc_fx,
    .pattern = is_pattern,
    .key_notes = is_key_notes,
    .mono = is_mono,
    .releases = is_releases,
    .format = is_format,
    .item_name = is_item_name,
    .press = is_press,
    .repress = is_repress,
    .knob = is_knob,
    .dim = is_dim,
    .draw = is_draw,
    .idle_ms = 160,
    .busy_ms = 40,
    .expect = is_expect,
};
