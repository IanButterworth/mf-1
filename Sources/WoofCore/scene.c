/* Woof's scene: a pixel-art dog in its place, drawn like an old cel cartoon, with flat blocks of colour set a
 * pixel off their scratchy ink lines. Plain C with integer arithmetic only, so the FM-1 firmware can draw the same
 * pictures. */
#include "WoofCore.h"

#define W WF_SCENE_W
#define H WF_SCENE_H
#define GROUND 57 /* the row the dog's feet stand on */

#define INK 0x1A1A1Au
#define PAPER 0xFAF7F0u

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

static int inside_ellipse(int x, int y, int cx, int cy, int rx, int ry)
{
    int dx = x - cx, dy = y - cy;
    if (rx <= 0 || ry <= 0)
        return rx == 0 && ry == 0 && dx == 0 && dy == 0;
    return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry;
}

static void disc(int cx, int cy, int rx, int ry, uint32_t c)
{
    int x, y;
    for (y = cy - ry; y <= cy + ry; y++)
        for (x = cx - rx; x <= cx + rx; x++)
            if (inside_ellipse(x, y, cx, cy, rx, ry))
                put(x, y, c);
}

static void ring(int cx, int cy, int rx, int ry, uint32_t c)
{
    int x, y;
    for (y = cy - ry; y <= cy + ry; y++)
        for (x = cx - rx; x <= cx + rx; x++)
            if (inside_ellipse(x, y, cx, cy, rx, ry) &&
                (!inside_ellipse(x - 1, y, cx, cy, rx, ry) || !inside_ellipse(x + 1, y, cx, cy, rx, ry) ||
                 !inside_ellipse(x, y - 1, cx, cy, rx, ry) || !inside_ellipse(x, y + 1, cx, cy, rx, ry)))
                put(x, y, c);
}

/* The cel look: colour laid down a pixel up and left of its ink outline. */
static void block(int x, int y, int w, int h, uint32_t fill)
{
    rect(x - 1, y - 1, w, h, fill);
    frame(x, y, w, h, INK);
}

static void blob(int cx, int cy, int rx, int ry, uint32_t fill)
{
    disc(cx - 1, cy - 1, rx, ry, fill);
    ring(cx, cy, rx, ry, INK);
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

/* ------------------------------------------------------------------ places */

static void tree(int x, int y, int r, int foot, uint32_t leaf)
{
    rect(x, y, 3, foot - y, 0x6B4A2Eu);
    rect(x + 2, y, 1, foot - y, INK);
    blob(x + 1, y - r + 2, r, r, leaf);
    disc(x - r / 3, y - r, r / 3, r / 4, mix(leaf, PAPER, 5));
}

static void park(int step, int far)
{
    int i;
    sky(0xF6E9C6u, 0xFBF3DCu, 40);
    for (i = 0; i < 6; i++)
        tree(5 + i * 21 + (i & 1) * 4, 30 + (i & 1) * 3, 6 + (i % 3) * 2, 40, (i & 1) ? 0x5E9E8Au : 0x7DB59Bu);
    rect(0, 40, W, H - 40, 0x9BC46Au);
    rect(0, 40, W, 1, INK);
    for (i = 0; i < 30; i++) {
        uint32_t h = hash(i, 21);
        put((int)(h % W), 43 + (int)((h >> 8) % 20u), 0x86B158u);
    }
    /* the path curving away */
    for (i = 0; i < H - 43; i++)
        rect(70 - i * 3 / 4 - (i * i) / 30, 43 + i, 10 + i * 3 / 2, 1, 0xEFDDB0u);
    /* a lamp post */
    rect(105, 17, 2, 27, INK);
    block(102, 12, 8, 6, 0xF2C14Eu);
    rect(103, 11, 6, 1, INK);
    if (far)
        rect(104, 14, 3, 2, (step & 1) ? 0xFFF2B0u : 0xF2C14Eu);
    /* a bench */
    block(6, 40, 20, 3, 0xC8553Du);
    rect(6, 37, 18, 1, 0xA8432Fu);
    rect(8, 43, 2, 5, INK);
    rect(22, 43, 2, 5, INK);
}

static void parlour(int step)
{
    int i;
    (void)step;
    rect(0, 0, W, 45, 0xE9D7A9u);
    for (i = 3; i < W; i += 7)
        rect(i, 0, 1, 45, 0xDCC690u);
    /* a window with red curtains */
    block(80, 6, 27, 22, 0x9CC9E2u);
    rect(93, 6, 1, 22, INK);
    rect(80, 16, 27, 1, INK);
    disc(86, 11, 3, 1, PAPER);
    block(76, 4, 6, 28, 0xC8392Fu);
    block(105, 4, 6, 28, 0xC8392Fu);
    rect(75, 3, 37, 2, INK);
    /* an upright piano */
    block(4, 19, 30, 26, 0x3B2A22u);
    rect(4, 29, 29, 3, PAPER);
    for (i = 5; i < 33; i += 3)
        rect(i, 29, 1, 2, INK);
    rect(4, 32, 29, 1, INK);
    block(9, 13, 9, 7, PAPER); /* sheet music */
    for (i = 0; i < 3; i++)
        rect(10, 15 + i * 2, 6, 1, INK);
    rect(6, 43, 2, 2, INK);
    rect(30, 43, 2, 2, INK);
    /* a picture on the wall */
    block(48, 10, 14, 10, 0xE8A23Fu);
    disc(54, 15, 3, 2, 0x5E9E8Au);
    /* the floor and a rug */
    rect(0, 45, W, H - 45, 0xB07A4Au);
    rect(0, 45, W, 1, INK);
    for (i = 0; i < W; i += 13)
        rect(i, 46, 1, H - 46, 0x9A6639u);
    disc(62, 57, 34, 5, 0xD45B3Au);
    disc(62, 57, 28, 3, 0xE8A23Fu);
    disc(62, 57, 20, 2, 0xD45B3Au);
}

static void rooftops(int step, int bark)
{
    int i;
    sky(0x23305Eu, 0xE07A5Fu, 44);
    for (i = 0; i < 22; i++) {
        uint32_t h = hash(i, 7);
        int x = (int)(h % W), y = (int)((h >> 8) % 20);
        if (((h >> 16) + (uint32_t)step) % 9u != 0)
            put(x, y, 0xFFF4D6u);
    }
    blob(96, 10, 6, 6, 0xFFF4D6u);
    disc(94, 9, 1, 1, 0xE8DCBCu);
    disc(98, 12, 1, 0, 0xE8DCBCu);
    /* far roofs: dark blocks with chimney pots and lit windows */
    for (i = 0; i < 6; i++) {
        int x = i * 21 - 3, top = 30 + (int)(hash(i, 3) % 8);
        rect(x, top, 20, 44 - top, 0x3A3F5Cu);
        rect(x + 4, top - 6, 5, 6, 0x4A4F6Eu);
        rect(x + 4, top - 8, 2, 2, 0x8A4A3Au);
        rect(x + 7, top - 8, 2, 2, 0x8A4A3Au);
        if (hash(i, step / 6) & 1)
            rect(x + 13, top + 4, 3, 3, 0xF2C14Eu);
    }
    /* far dogs on the roofs, barking back after the dog does */
    if (bark > 0) {
        for (i = 0; i < 2; i++) {
            int x = 26 + i * 70, y = 28 + i * 2;
            rect(x, y, 6, 3, INK);
            rect(x + 5, y - 3, 3, 3, INK);
            put(x + 8, y - 2, INK);
            put(x - 1, y - 1, INK);
            rect(x, y + 3, 1, 2, INK);
            rect(x + 5, y + 3, 1, 2, INK);
            if ((step + i) & 1) {
                put(x + 10, y - 4, 0xFFF4D6u);
                put(x + 11, y - 2, 0xFFF4D6u);
                put(x + 10, y, 0xFFF4D6u);
            }
        }
    }
    /* the near roof the dog stands on, with a chimney */
    rect(0, 44, W, H - 44, 0x2A2D40u);
    rect(0, 44, W, 1, INK);
    for (i = 0; i < W; i += 6)
        rect(i, 48 + (i / 6 & 1) * 5, 4, 1, 0x3B3F58u);
    block(8, 26, 12, 18, 0x9A4A3Au);
    for (i = 0; i < 4; i++)
        rect(8, 30 + i * 4, 11, 1, 0x7E3A2Eu);
    rect(6, 23, 16, 3, INK);
    rect(10, 20, 3, 3, 0x8A4A3Au);
    rect(15, 20, 3, 3, 0x8A4A3Au);
}

static void farm(int step)
{
    int i;
    sky(0x8B9DC0u, 0xD9DFEAu, 40);
    /* a red barn and a fence */
    block(78, 19, 30, 22, 0xB33A2Eu);
    for (i = 0; i < 9; i++)
        rect(77 + i, 19 - i, 32 - i * 2, 1, INK);
    for (i = 0; i < 8; i++)
        rect(78 + i, 19 - i, 30 - i * 2, 1, 0x8C2A22u);
    block(88, 29, 10, 12, PAPER);
    rect(89, 29, 8, 1, INK);
    rect(89, 30, 1, 10, INK);
    rect(96, 30, 1, 10, INK);
    for (i = 0; i < 9; i++)
        put(89 + i, 30 + i, INK);
    block(91, 22, 4, 4, 0xF2C14Eu);
    rect(0, 40, W, H - 40, 0xF2F4F7u);
    rect(0, 40, W, 1, 0x9FAEC6u);
    for (i = 0; i < 70; i += 9) {
        rect(i, 32, 2, 9, INK);
        rect(i - 1, 34, 10, 1, 0x6B4A2Eu);
        rect(i - 1, 37, 10, 1, 0x6B4A2Eu);
    }
    for (i = 0; i < 10; i++)
        disc(10 + i * 13, 53 + (i & 1) * 4, 8, 1, 0xDCE3EEu);
    /* falling snow */
    for (i = 0; i < 40; i++) {
        uint32_t h = hash(i, 11);
        int x = (int)((h + (uint32_t)step * (1u + (h >> 30))) % W);
        int y = (int)((h / W + (uint32_t)step * 3u) % (H - 4));
        put(x, y, PAPER);
        if (i % 5 == 0)
            put(x + 1, y, PAPER);
    }
}

static void river(int step)
{
    int i;
    sky(0xA9CFE3u, 0xE6F1F4u, 33);
    /* a far town and a flat bridge */
    for (i = 0; i < 10; i++) {
        int h = 6 + (int)(hash(i, 5) % 10);
        rect(i * 13, 33 - h, 12, h, 0x8FA6B8u);
        if (hash(i, 9) & 1)
            rect(i * 13 + 4, 33 - h - 4, 3, 4, 0x8FA6B8u);
        if (hash(i, 13) & 1)
            rect(i * 13 + 3, 33 - h + 3, 2, 2, 0xB4C6D4u);
    }
    rect(0, 33, W, 13, 0x4F86B0u);
    rect(27, 27, 66, 3, 0x6E6A66u);
    rect(27, 26, 66, 1, INK);
    for (i = 0; i < 5; i++)
        rect(30 + i * 14, 30, 3, 7, 0x6E6A66u);
    for (i = 0; i < 12; i++)
        rect((int)((hash(i, 2) + (uint32_t)step) % W), 37 + i % 8, 5, 1, 0x7FAED0u);
    rect(0, 46, W, H - 46, 0x8DB86Au);
    rect(0, 46, W, 1, INK);
    for (i = 0; i < 10; i++) {
        int x = 5 + i * 12 + (i & 1) * 4;
        rect(x, 41, 1, 6, 0x4E7A3Cu);
        rect(x + 2, 43, 1, 4, 0x4E7A3Cu);
        put(x, 40, 0x7A5A3Au);
    }
}

/* ------------------------------------------------------------------ dogs */

enum { EAR_FLOP, EAR_UP, EAR_LONG, EAR_FOLD, EAR_FLUFF };
enum { TAIL_UP, TAIL_CURL, TAIL_STUB, TAIL_THIN, TAIL_POM, TAIL_SICKLE };
enum { COAT_SOLID, COAT_SPOTS, COAT_SADDLE, COAT_MASK, COAT_BIB, COAT_PATCH, COAT_CURLS, COAT_WAVY };

typedef struct {
    uint32_t coat, patch;
    int pattern, bw, bh, leg, head, snout, ear, tail;
} look_t;

/* In the order of the breeds. Sizes are in scene pixels: body length and depth, leg, head and muzzle. */
#define LOOK_COUNT 13
static const look_t LOOKS[LOOK_COUNT] = {
    {0xFAF7F0u, INK, COAT_SPOTS, 30, 11, 12, 11, 6, EAR_FLOP, TAIL_UP},           /* Dalmatian */
    {0xE3B45Cu, 0xC8963Eu, COAT_SOLID, 32, 12, 10, 11, 6, EAR_FLOP, TAIL_UP},     /* Labrador */
    {0xD9A06Au, 0xF4E2C4u, COAT_BIB, 15, 7, 7, 10, 3, EAR_UP, TAIL_SICKLE},       /* Chihuahua */
    {0xFAF7F0u, 0xA0602Eu, COAT_SADDLE, 24, 10, 8, 10, 5, EAR_LONG, TAIL_SICKLE}, /* Beagle */
    {0x7D8696u, 0xF4F4F2u, COAT_BIB, 30, 12, 10, 11, 6, EAR_UP, TAIL_CURL},       /* Husky */
    {0xF4EEE4u, 0xA9825Eu, COAT_PATCH, 25, 12, 6, 12, 2, EAR_FOLD, TAIL_STUB},    /* Bulldog */
    {0xF6F1EAu, 0xE6DCCFu, COAT_CURLS, 22, 9, 12, 10, 6, EAR_FLUFF, TAIL_POM},    /* Poodle */
    {0xE0873Au, 0xFAF3E6u, COAT_BIB, 25, 10, 4, 10, 5, EAR_UP, TAIL_STUB},        /* Corgi */
    {0xA9522Eu, 0x7A3A20u, COAT_SOLID, 34, 8, 4, 10, 6, EAR_LONG, TAIL_THIN},     /* Dachshund */
    {0xB9824Au, 0x2A2420u, COAT_SADDLE, 31, 12, 12, 11, 7, EAR_UP, TAIL_THIN},    /* Shepherd */
    {0x8E9AAEu, 0x6F7A8Eu, COAT_SOLID, 36, 13, 16, 12, 7, EAR_FLOP, TAIL_THIN},   /* Great Dane */
    {0xD9B98Au, 0x3A302Au, COAT_MASK, 18, 9, 5, 10, 2, EAR_FOLD, TAIL_CURL},      /* Pug */
    {0x8A5A3Au, 0x6A4026u, COAT_WAVY, 26, 11, 10, 11, 5, EAR_LONG, TAIL_UP},      /* Otto, a cockapoo */
};

/* The dog is drawn as parts into a mask first, then outlined in ink, so every shape joins up. */
enum { M_NONE, M_COAT, M_PATCH, M_INK, M_COLLAR, M_MOUTH, M_WHITE, M_SHADE, M_TONGUE, M_INNER, M_TAG, M_SHINE };
static uint8_t mask[W * H];

static void mput(int x, int y, int v)
{
    if (x >= 0 && x < W && y >= 0 && y < H)
        mask[y * W + x] = (uint8_t)v;
}

static void mrect(int x, int y, int w, int h, int v)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            mput(i, j, v);
}

static void mdisc(int cx, int cy, int rx, int ry, int v)
{
    int x, y;
    for (y = cy - ry; y <= cy + ry; y++)
        for (x = cx - rx; x <= cx + rx; x++)
            if (inside_ellipse(x, y, cx, cy, rx, ry))
                mput(x, y, v);
}

static int absi(int v) { return v < 0 ? -v : v; }

/* A thick line, for tails. */
static void mline(int x0, int y0, int x1, int y1, int thick, int v)
{
    int n = absi(x1 - x0) > absi(y1 - y0) ? absi(x1 - x0) : absi(y1 - y0), i;
    if (n == 0)
        n = 1;
    for (i = 0; i <= n; i++)
        mrect(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, thick, thick, v);
}

static int mget(int x, int y)
{
    return x >= 0 && x < W && y >= 0 && y < H ? mask[y * W + x] : M_NONE;
}

typedef struct {
    int x0;           /* the body's left edge */
    int lift;         /* how far the dog is off the ground, for a hop */
    int walk;         /* leg phase while trotting, -1 standing */
    int barking, howling, growling;
    int wag;          /* tail position, 0..2 */
    int sad, glad;
} pose_t;

static void leg(int x, int top, int len, int lw, int swing, int v, int poms)
{
    int dx = swing > 0 ? 1 : 0, l = len + (swing < 0 ? -1 : 0);
    mrect(x + dx, top, lw, l, v);
    mrect(x + dx, top + l - 2, lw + 1, 2, v); /* the paw */
    if (poms)
        mdisc(x + dx + lw / 2, top + l - 2, lw / 2 + 1, 2, M_PATCH);
}

static void draw_dog(const look_t *L, int size, const pose_t *p, uint32_t collar)
{
    int bw = L->bw + size, bh = L->bh + size / 3, lg = L->leg + size / 3, head = L->head + size / 4;
    int bx, by, hx, hy, hr, sx, sy, sn = L->snout, lw, i, x, y, belly, jaw;
    uint32_t shade = mix(L->coat, INK, 4), shadeP = mix(L->patch, INK, 3);
    int poms = L->pattern == COAT_CURLS;
    if (bw < 10)
        bw = 10;
    if (bh < 5)
        bh = 5;
    if (lg < 2)
        lg = 2;
    if (head < 6)
        head = 6;
    lw = bw >= 28 ? 4 : bw >= 18 ? 3 : 2;
    bx = p->x0;
    by = GROUND - p->lift - lg - bh + 2;
    belly = by + bh * 2 / 3;

    /* far legs first, in shade, a little behind the near ones */
    for (i = 0; i < 2; i++) {
        int swing = p->walk < 0 ? 0 : (((p->walk + i) & 1) ? -1 : 1);
        int lx = i == 0 ? bx + 3 + lw : bx + bw - lw - 3;
        leg(lx, belly - 1, GROUND - p->lift - belly + 1, lw, swing, M_SHADE, poms);
    }
    /* body: a rump and a deeper chest, joined along the back, with the belly tucked up between */
    mdisc(bx + bh / 2, by + bh / 2, bh / 2, bh / 2, M_COAT);
    mdisc(bx + bw - bh / 2 - 1, by + bh / 2 + 1, bh / 2 + 1, bh / 2 + 1, M_COAT);
    mrect(bx + bh / 2, by, bw - bh, bh * 2 / 3 + 1, M_COAT);
    mdisc(bx + bw / 2, by + bh / 3, bw / 2 - 1, bh / 3, M_COAT);
    /* the haunch over the back leg */
    mdisc(bx + bh / 2 + 1, by + bh * 2 / 3, bh / 3 + 1, bh / 2, M_COAT);
    /* near legs */
    for (i = 0; i < 2; i++) {
        int swing = p->walk < 0 ? 0 : (((p->walk + i) & 1) ? 1 : -1);
        int lx = i == 0 ? bx + 1 : bx + bw - lw * 2 - 2;
        leg(lx, belly - 1, GROUND - p->lift - belly + 1, lw, swing, M_COAT, poms);
    }

    /* tail, from the rump */
    x = bx + 1, y = by + 2;
    switch (L->tail) {
    case TAIL_UP:
    case TAIL_SICKLE:
        if (p->sad) {
            mline(x, y, x - 4, y + 4, 2, M_COAT);
            mline(x - 4, y + 4, x - 5, y + 9, 2, M_COAT);
        } else {
            int lean = (p->wag - 1) * 2;
            mline(x, y, x - 4, y - 3, 2, M_COAT);
            mline(x - 4, y - 3, x - 5 + lean, y - 8, 2, M_COAT);
            mline(x - 5 + lean, y - 8, x - 4 + lean * 2, y - 11, 1, L->tail == TAIL_SICKLE ? M_PATCH : M_COAT);
        }
        break;
    case TAIL_CURL:
        mdisc(x + 2, y - 3, 3, 3, M_COAT);
        mdisc(x + 2, y - 3, 1, 1, M_NONE);
        mput(x + 2, y - 2, M_NONE);
        break;
    case TAIL_STUB:
        mdisc(x - 1, y + (p->wag == 1 ? 0 : 1), 2, 2, M_COAT);
        break;
    case TAIL_THIN:
        mline(x, y + 1, x - 4, y + 3, 2, M_COAT);
        if (p->sad)
            mline(x - 4, y + 3, x - 5, y + 12, 2, M_COAT);
        else
            mline(x - 4, y + 3, x - 7 - (p->wag - 1) * 2, y + 9, 2, M_COAT);
        break;
    case TAIL_POM:
        mline(x, y, x - 2, y - 5, 1, M_COAT);
        mdisc(x - 3 + (p->wag - 1), y - 7, 3, 3, M_PATCH);
        break;
    }

    /* head: up on a neck at the front, lower when sad or growling, up when howling */
    hr = head / 2;
    hx = bx + bw - 3;
    hy = by - hr + 1 + (p->sad || p->growling ? 3 : 0) - (p->howling ? 3 : 0) - (p->barking && !p->howling ? 1 : 0);
    /* the neck, from the chest up into the head */
    for (y = hy; y <= by + bh / 2; y++) {
        int t = (y - hy) * 4 / (by + bh / 2 - hy + 1);
        mrect(hx - hr + 1 - t, y, hr + 2 + t, 1, M_COAT);
    }
    mdisc(hx, hy, hr, hr, M_COAT);
    /* collar, with a tag */
    for (y = hy + hr - 1; y <= hy + hr + 1; y++)
        for (x = hx - hr - 3; x <= hx + hr; x++)
            if (mget(x, y) == M_COAT && (y - (hy + hr - 1)) * 2 + (x - (hx - hr - 3)) / 3 < 7)
                mput(x, y, M_COLLAR);
    mrect(hx - 1, hy + hr + 2, 2, 2, M_TAG);

    /* the muzzle: a block from the stop forward, tapering at the top; a howl tips it up */
    sx = hx + hr - 1;
    sy = hy - 1 - (p->howling ? 2 : 0);
    jaw = p->barking ? (p->howling ? 2 : 3) : 0;
    if (sn > 0) {
        int mh = hr > 3 ? hr : 3;
        for (i = 0; i < sn + 1; i++) {
            int tip = p->howling ? i / 3 : 0;
            int top = sy + (i * 2 > sn ? 1 : 0) - tip;
            mrect(sx + i, top, 1, mh - (top - sy) - tip, M_COAT);
            /* the lower jaw, dropped open for a bark */
            mrect(sx + i - (i == sn ? 1 : 0), sy + mh + jaw - tip, 1, 2, M_COAT);
            if (jaw && i < sn - 1)
                mrect(sx + i, sy + mh - tip, 1, jaw, M_MOUTH);
        }
        if (jaw) {
            mrect(sx + 1, sy + mh + jaw - 1, sn / 2 + 1, 1, M_TONGUE);
            mput(sx + sn - 2, sy + mh - (p->howling ? (sn - 2) / 3 : 0), M_WHITE); /* a tooth */
            if (p->growling)
                mput(sx + sn - 4, sy + mh, M_WHITE);
        } else {
            mrect(sx + 1, sy + mh, sn - 1, 1, M_INK); /* the closed mouth */
        }
        /* nose, with a shine */
        y = sy + 1 - (p->howling ? sn / 3 : 0);
        mrect(sx + sn - 1, y, 2, 2, M_INK);
        mput(sx + sn - 1, y, M_SHINE);
    } else {
        /* a flat face: a wide jowl and a nose on the front of the head */
        mdisc(hx + hr - 1, hy + 2, 3, 3, M_COAT);
        mrect(hx + hr + 1, hy, 2, 2, M_INK);
        mput(hx + hr + 1, hy, M_SHINE);
        if (jaw) {
            mrect(hx + hr - 2, hy + 4, 4, jaw, M_MOUTH);
            mrect(hx + hr - 1, hy + 3 + jaw, 2, 1, M_TONGUE);
        } else {
            mrect(hx + hr - 1, hy + 4, 3, 1, M_INK);
        }
    }
    /* ears */
    switch (L->ear) {
    case EAR_FLOP:
        mdisc(hx - hr / 2, hy - hr / 2 + 2, hr / 2, hr / 2 + 2 + (p->sad ? 1 : 0), M_PATCH);
        mrect(hx - hr / 2 - 1, hy - hr + 1, hr / 2 + 2, 2, M_PATCH);
        break;
    case EAR_UP:
        if (p->sad || p->growling) {
            mline(hx - hr + 1, hy - hr + 2, hx - hr - 4, hy - hr + 3, 2, M_COAT);
        } else {
            int tall = hr + (L->bw < 18 ? 3 : 1), wide = hr - 1;
            for (i = 0; i < tall; i++) {
                int w = wide - i * wide / tall, ex = hx - hr / 2 - w / 2, ey = hy - hr + 1 - i;
                if (w < 1)
                    w = 1;
                mrect(ex, ey, w, 1, i > 0 && i < tall - 2 && w > 2 ? M_INNER : M_COAT);
                mput(ex, ey, M_COAT);
                mput(ex + w - 1, ey, M_COAT);
            }
        }
        break;
    case EAR_LONG:
        mrect(hx - hr / 2 - 2, hy - hr + 1, hr / 2 + 3, 2, M_PATCH);
        mdisc(hx - hr / 2, hy + 1 + (p->barking ? -1 : 0), hr / 2 + 1, hr - 1, M_PATCH);
        break;
    case EAR_FOLD:
        mrect(hx - hr / 2 - 1, hy - hr, 3, 2, M_PATCH);
        mput(hx - hr / 2 + 2, hy - hr + 2, M_PATCH);
        break;
    case EAR_FLUFF:
        mdisc(hx - hr / 2, hy + 1, hr / 2 + 1, hr - 1, M_PATCH);
        mdisc(hx, hy - hr, hr - 1, hr / 2, M_PATCH); /* the topknot */
        break;
    }

    /* eye and brow: a shining dot, or shut and sloped when sad */
    x = hx + hr / 3, y = hy - hr / 3;
    if (p->sad) {
        mrect(x - 1, y + 1, 3, 1, M_INK);
    } else {
        mrect(x, y, 2, 2, M_INK);
        mput(x, y, M_SHINE);
        mrect(x - 1, y - 2 + (p->growling ? 1 : 0), 3, 1, M_SHADE);
    }

    /* coat patterns, kept off the outline so the dog's shape stays clear */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int m = mget(x, y), lx = x - bx + 60, ly = y - by + 60;
            if (m != M_COAT && m != M_SHADE)
                continue;
            if (mget(x - 1, y) == M_NONE || mget(x + 1, y) == M_NONE || mget(x, y - 1) == M_NONE || mget(x, y + 1) == M_NONE)
                continue;
            switch (L->pattern) {
            case COAT_SPOTS: {
                /* round black spots, one or none to each 5 by 4 cell, two or three pixels across */
                uint32_t h = hash(lx / 5, ly / 4);
                int ox = (int)(h % 3u), oy = (int)((h >> 4) % 2u), big = (int)((h >> 12) & 1u);
                if ((h >> 8) % 5u < 3u && lx % 5 >= ox && lx % 5 < ox + 2 + big && ly % 4 >= oy && ly % 4 < oy + 2)
                    mput(x, y, M_PATCH);
                break;
            }
            case COAT_SADDLE:
                if (y < by + bh / 2 + 1 && x > bx + 3 && x < bx + bw - 4 && y >= by - 1)
                    mput(x, y, M_PATCH);
                break;
            case COAT_MASK:
                if (x >= sx - 3 && y >= sy - 2 && y <= sy + hr + 3)
                    mput(x, y, M_PATCH);
                break;
            case COAT_BIB:
                if ((y > belly - 2 && x > bx + bw / 3) || (x >= sx && y >= sy + 2) ||
                    (x >= hx - 2 && x <= hx + hr && y > hy + 1 && y < hy + hr - 1))
                    mput(x, y, M_PATCH);
                break;
            case COAT_PATCH:
                if (inside_ellipse(x, y, bx + bw / 3, by + 3, 5, 4) || inside_ellipse(x, y, hx + hr / 3, hy - hr / 3, 3, 3))
                    mput(x, y, M_PATCH);
                break;
            case COAT_CURLS:
                if (((x + y * 2) % 3 == 0) && (y < by + 3 || x < bx + bh / 2 + 1))
                    mput(x, y, M_PATCH);
                break;
            case COAT_WAVY:
                /* loose curls all over, with no clipped poms */
                if ((x + y * 2) % 4 == 0 || (x * 3 + y) % 7 == 0)
                    mput(x, y, M_PATCH);
                break;
            }
        }
    /* shading: the underside of the body, in a darker coat */
    for (y = belly; y < belly + 4; y++)
        for (x = bx + 2; x < bx + bw - 2; x++)
            if (mget(x, y) == M_COAT && mget(x, y + 1) == M_NONE)
                mput(x, y, M_SHADE);

    /* ink outline round the whole dog, then the colours */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if (mget(x, y) == M_NONE &&
                (mget(x - 1, y) != M_NONE || mget(x + 1, y) != M_NONE || mget(x, y - 1) != M_NONE || mget(x, y + 1) != M_NONE))
                put(x, y, INK);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int m = mget(x, y);
            uint32_t c;
            switch (m) {
            case M_NONE: continue;
            case M_COAT: c = L->coat; break;
            case M_PATCH: c = L->patch; break;
            case M_SHADE: c = L->pattern == COAT_SPOTS ? mix(L->coat, 0x8090B0u, 5) : shade; break;
            case M_COLLAR: c = collar; break;
            case M_MOUTH: c = 0x6A1E22u; break;
            case M_TONGUE: c = 0xE86A7Au; break;
            case M_INNER: c = 0xE8A0A0u; break;
            case M_TAG: c = 0xF2C14Eu; break;
            case M_WHITE: c = PAPER; break;
            case M_SHINE: c = 0xC8C8C8u; break;
            default: c = INK; break;
            }
            /* the cel look: the fill runs a shade darker along the right-hand line */
            if ((m == M_COAT || m == M_PATCH) && mget(x + 1, y) == M_NONE)
                c = m == M_PATCH ? shadeP : shade;
            put(x, y, c);
        }

    /* bark lines ahead of the mouth, or a howl's rising notes */
    if (p->barking) {
        int bxl = sx + (sn > 0 ? sn : 2) + 4, byl = sy + hr / 2 + 1;
        if (p->howling) {
            for (i = 0; i < 3; i++) {
                rect(bxl + i * 3, byl - 5 - i * 4, 2, 1, INK);
                put(bxl + i * 3 + 1, byl - 6 - i * 4, INK);
            }
        } else {
            for (i = 0; i < 3; i++) {
                put(bxl + i, byl - 4 - i, INK);
                rect(bxl + 1 + i, byl, 2, 1, INK);
                put(bxl + i, byl + 4 + i, INK);
            }
        }
    }
}

/* ------------------------------------------------------------------ the scene */

void woof_scene(const int32_t dog[WF_FIELDS], int step, int bark, int arrived, uint32_t out[WF_SCENE_W * WF_SCENE_H])
{
    int32_t s[WF_FIELDS];
    const look_t *L;
    pose_t p;
    int i, size, dogs = 1, target, mood;
    WoofDog info;
    for (i = 0; i < WF_FIELDS; i++)
        s[i] = dog[i];
    woof_clamp(s);
    px = out;
    L = &LOOKS[s[WF_DOG] % LOOK_COUNT];
    woof_dog(s[WF_DOG], &info);
    mood = s[WF_MOOD];

    switch (s[WF_PLACE]) {
    case WF_PARLOUR: parlour(step); break;
    case WF_ROOFTOPS: rooftops(step, bark); break;
    case WF_FARM: farm(step); break;
    case WF_RIVER: river(step); break;
    default: park(step, s[WF_DISTANCE] > 40); break;
    }

    /* Size grows or shrinks the dog by a few pixels. */
    size = (s[WF_SIZE] - 50) / 8;
    p.barking = bark > 0;
    p.howling = p.barking && s[WF_HOWL] >= 50;
    p.growling = s[WF_GROWL] >= 50;
    p.sad = mood < 31 && !p.barking;
    p.glad = mood >= 71;
    p.lift = p.glad && p.barking && (step & 1) ? 1 : 0;
    p.wag = p.sad ? 1 : p.glad ? (step % 2) * 2 : (step / 2) % 3;
    target = W / 2 - (L->bw + size) / 2 - 6;
    p.walk = -1;
    p.x0 = target;
    if (arrived >= 0 && arrived < 10) {
        p.x0 = target - (10 - arrived) * 8;
        p.walk = step;
        p.barking = p.howling = 0;
    }

    /* A chorus or chase brings the rest of the pack in: smaller copies behind. */
    if (s[WF_PLAY] == WF_CHORUS || s[WF_PLAY] == WF_CHASE)
        dogs = s[WF_PACK] == WF_PAIR ? 2 : s[WF_PACK] == WF_TRIO ? 3 : 5;
    if (dogs > 1) {
        int k;
        for (k = dogs - 1; k >= 1; k--) {
            int slot = k - 1;
            pose_t q = p;
            q.x0 = p.x0 + (slot % 2 ? 30 + slot * 4 : -30 - slot * 4) * (dogs == 5 ? 2 : 3) / 3;
            q.lift = 0;
            q.barking = p.barking && ((step + k) & 1);
            q.howling = q.barking && p.howling;
            q.wag = (p.wag + k) % 3;
            for (i = 0; i < W * H; i++)
                mask[i] = M_NONE;
            draw_dog(L, dogs == 5 ? -12 : -7, &q, info.accent);
        }
    }
    for (i = 0; i < W * H; i++)
        mask[i] = M_NONE;
    draw_dog(L, size, &p, s[WF_DOG] == 0 ? 0xD8282Eu : info.accent);
}
