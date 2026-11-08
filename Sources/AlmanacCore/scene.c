// Almanac's scene: a small pixel-art landscape for the month, its sky and its place, drawn a few
// frames a second. Plain C with integer arithmetic, so the FM-1 firmware draws the same pictures.
#include "AlmanacCore.h"

#define W AL_SCENE_W
#define H AL_SCENE_H

typedef uint32_t rgb;

static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
  uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  return h;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// a to b by t out of 256.
static rgb mix(rgb a, rgb b, int t) {
  t = clampi(t, 0, 256);
  int r = (int)((a >> 16) & 255) + ((((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t) >> 8);
  int g = (int)((a >> 8) & 255) + ((((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t) >> 8);
  int bl = (int)(a & 255) + ((((int)(b & 255) - (int)(a & 255)) * t) >> 8);
  return (rgb)((r << 16) | (g << 8) | bl);
}

static rgb darken(rgb c, int t) { return mix(c, 0x000000, t); }
static rgb lighten(rgb c, int t) { return mix(c, 0xFFFFFF, t); }

typedef struct {
  rgb* px;
  int step;
} Canvas;

static void put(Canvas* c, int x, int y, rgb color) {
  if (x >= 0 && x < W && y >= 0 && y < H) c->px[y * W + x] = color;
}

static void blend(Canvas* c, int x, int y, rgb color, int t) {
  if (x >= 0 && x < W && y >= 0 && y < H) c->px[y * W + x] = mix(c->px[y * W + x], color, t);
}

static void rect(Canvas* c, int x, int y, int w, int h, rgb color) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) put(c, i, j, color);
}

static void disc(Canvas* c, int cx, int cy, int r, rgb color) {
  for (int j = -r; j <= r; j++)
    for (int i = -r; i <= r; i++)
      if (i * i + j * j <= r * r + r / 2) put(c, cx + i, cy + j, color);
}

// A puffy cloud, w pixels wide, drawn as a row of overlapping discs on a flat base.
static void cloud(Canvas* c, int x, int y, int w, rgb color, rgb shade) {
  int r = 2 + w / 8;
  for (int i = 0; i <= w; i += r) disc(c, x + i, y - (i % (2 * r) == r ? 1 : 0), r, color);
  rect(c, x - r + 1, y + 1, w + 2 * r - 1, r, color);
  for (int i = x - r + 2; i < x + w + r - 1; i++) put(c, i, y + r, shade);
}

// The height of the far land at x, as a row number: smaller is higher.
static int skyline(int place, int x) {
  switch (place) {
    case AL_MOUNTAINS: {
      static const int peaks[5][2] = {{4, 15}, {20, 9}, {38, 16}, {55, 7}, {74, 13}};
      int best = 30;
      for (int i = 0; i < 5; i++) {
        int d = x - peaks[i][0];
        if (d < 0) d = -d;
        int y = peaks[i][1] + d * 4 / 5;
        if (y < best) best = y;
      }
      return best;
    }
    case AL_FOREST:
      return 22 + (int)(hash3((uint32_t)x / 3, 7, 1) % 3);
    case AL_COAST:
      return 27;
    case AL_VILLAGE:
    case AL_LAKE:
    case AL_MEADOW:
    default: {
      // Rolling hills: two triangle waves added.
      int a = (x * 2) % 40, b = (x + 13) % 26;
      a = a < 20 ? a : 40 - a;
      b = b < 13 ? b : 26 - b;
      return 26 - a / 5 - b / 5;
    }
  }
}

static void pine(Canvas* c, int x, int base, int h, rgb color, rgb snow) {
  for (int j = 0; j < h; j++) {
    int half = j / 2;
    for (int i = -half; i <= half; i++) put(c, x + i, base - h + j, j == 0 && snow ? snow : color);
  }
  if (snow)
    for (int i = -1; i <= 1; i++) put(c, x + i, base - h + 2, snow);
  put(c, x, base, darken(color, 120));
}

static void house(Canvas* c, int x, int base, rgb wall, rgb roof, bool lit, bool smoke, int step) {
  rect(c, x, base - 4, 6, 4, wall);
  for (int j = 0; j < 3; j++) rect(c, x - 1 + j, base - 5 - j, 8 - 2 * j, 1, roof);
  put(c, x + 2, base - 2, lit ? 0xFFD65A : darken(wall, 90));
  put(c, x + 4, base - 3, lit ? 0xFFC23A : darken(wall, 90));
  if (smoke)
    for (int k = 0; k < 3; k++) {
      int dy = (step + k * 2) % 6;
      blend(c, x + 5 + (dy + k) / 3, base - 8 - dy, 0xE6E6EA, 150 - dy * 20);
    }
}

void almanac_scene(const int32_t day[AL_FIELDS], int step, int activity, uint32_t out[AL_SCENE_W * AL_SCENE_H]) {
  int32_t s[AL_FIELDS];
  for (int i = 0; i < AL_FIELDS; i++) s[i] = day[i];
  almanac_clamp(s);
  AlmanacMonth m;
  almanac_month(s[AL_MONTH], &m);
  Canvas cv = {out, step};
  Canvas* c = &cv;
  const int sky = s[AL_SKY], place = s[AL_PLACE], weather = s[AL_WEATHER], elements = s[AL_ELEMENTS];
  const int gusts = s[AL_GUSTS], season = m.season, month = s[AL_MONTH];
  const bool night = month == 11;
  activity = clampi(activity, 0, 100);

  // The sky, in a few bands as pixel art skies are, greyed by the weather.
  rgb overcast = 0;
  int grey = 0;
  switch (sky) {
    case AL_RAIN: overcast = 0x7E8794, grey = 90 + weather; break;
    case AL_FOG: overcast = 0xB9BDC2, grey = 110 + weather; break;
    case AL_STORM: overcast = 0x343B4A, grey = 120 + weather; break;
    case AL_SNOW: overcast = 0xD5DBE3, grey = 60 + weather; break;
    default: break;
  }
  const int horizon = 30;
  for (int y = 0; y < H; y++) {
    int band = clampi(y * 6 / horizon, 0, 5);
    rgb col = mix(m.skyTop, m.skyBottom, band * 256 / 5);
    if (grey) col = mix(col, overcast, grey);
    for (int x = 0; x < W; x++) put(c, x, y, col);
  }

  // Stars on a December evening, twinkling.
  if (night && sky != AL_STORM && sky != AL_FOG)
    for (int i = 0; i < 18; i++) {
      int x = (int)(hash3(i, 1, 9) % W), y = (int)(hash3(i, 2, 9) % 18);
      bool on = (hash3(i, (uint32_t)step / 2, 3) & 3) != 0;
      if (on) put(c, x, y, (hash3(i, 3, 9) & 1) ? 0xFFF6D0 : 0xC8D4FF);
    }

  // Sun, or the moon in December. Hidden behind heavy weather, pale through snow.
  if (sky == AL_CLEAR || sky == AL_BREEZE || sky == AL_SNOW) {
    int sx = 14 + season * 14, sy = season == 2 ? 6 : 9;
    if (night) {
      disc(c, 62, 7, 3, 0xF4F0D6);
      disc(c, 63, 6, 2, mix(m.skyTop, m.skyBottom, 40));
    } else {
      rgb sun = season == 2 ? 0xFFE066 : season == 0 ? 0xFFF4C8 : 0xFFD27A;
      if (sky == AL_SNOW) sun = mix(sun, overcast, 150);
      disc(c, sx, sy, season == 2 ? 4 : 3, sun);
      // When notes sound, the sun throws out a few rays.
      if (activity > 0 && sky != AL_SNOW)
        for (int k = 0; k < 8; k++) {
          static const int dx[8] = {6, 0, -6, 0, 4, -4, 4, -4}, dy[8] = {0, 6, 0, -6, 4, 4, -4, -4};
          if ((k + step) % 2 == 0) put(c, sx + dx[k], sy + dy[k], lighten(sun, 80));
        }
    }
  }

  // Clouds drift with the gusts: a few in a breeze, a lid of them in rain and storm.
  int drift = step * (1 + gusts / 34);
  if (sky == AL_BREEZE || sky == AL_CLEAR) {
    int n = sky == AL_BREEZE ? 3 : 1;
    for (int i = 0; i < n; i++) {
      int span = W + 30;
      int x = (int)((hash3(i, 5, 5) % span + drift / (i + 1)) % span) - 15;
      cloud(c, x, 6 + i * 5, 8 + i * 3, night ? 0x5A6788 : 0xFAFCFF, night ? 0x4A5778 : 0xD8E2EE);
    }
  } else if (sky == AL_RAIN || sky == AL_STORM || sky == AL_SNOW) {
    rgb top = sky == AL_STORM ? 0x2A303C : sky == AL_RAIN ? 0x6C7480 : 0xE4E8EE;
    rgb under = sky == AL_STORM ? 0x20252F : sky == AL_RAIN ? 0x5A626E : 0xC9D0DA;
    int low = sky == AL_STORM ? 12 : 9;
    for (int i = 0; i < 7; i++) {
      int span = W + 24;
      int x = (int)((hash3(i, 6, 5) % span + drift) % span) - 12;
      cloud(c, x, 1 + (int)(hash3(i, 7, 5) % (low - 2)), 10 + (int)(hash3(i, 8, 5) % 8), top, under);
    }
  }

  // Lightning in a storm: more often the more is played. The whole sky flashes for one frame.
  bool flash = false;
  if (sky == AL_STORM) {
    int every = 14 - activity / 10 - weather / 20;
    if (every < 3) every = 3;
    flash = hash3((uint32_t)step, 11, 13) % (uint32_t)every == 0;
    if (flash) {
      for (int i = 0; i < W * horizon; i++) c->px[i] = mix(c->px[i], 0xE8EEFF, 150);
      int x = 10 + (int)(hash3((uint32_t)step, 12, 13) % 60);
      for (int y = 10; y < 26; y++) {
        x += (int)(hash3((uint32_t)step, (uint32_t)y, 14) % 3) - 1;
        put(c, x, y, 0xFFFFF0);
      }
    }
  }

  // The far land, hazed by distance.
  rgb far = mix(m.far, m.skyBottom, 40 + s[AL_DISTANCE]);
  if (grey) far = mix(far, overcast, grey / 2);
  if (flash) far = lighten(far, 60);
  for (int x = 0; x < W; x++) {
    int top = skyline(place, x);
    for (int y = top; y < 34; y++) put(c, x, y, far);
    if (place == AL_MOUNTAINS && season != 2)
      for (int y = top; y < top + 2 && y < 34; y++) put(c, x, y, top < 13 ? 0xF4F8FC : far);
  }
  if (place == AL_FOREST) {
    rgb tree = mix(darken(m.far, 70), 0x1E4A2A, season == 3 ? 60 : 140);
    if (season == 3) tree = mix(tree, 0xB0531E, 120);
    for (int x = 1; x < W; x += 5) pine(c, x + (int)(hash3(x, 1, 2) % 3), 32, 7 + (int)(hash3(x, 2, 2) % 5), tree,
                                         season == 0 ? 0xF4F8FC : 0);
  }

  // The near ground, or water for the coast and the lake.
  int groundTop = 33;
  rgb ground = m.ground;
  if (grey) ground = mix(ground, overcast, grey / 3);
  if (flash) ground = lighten(ground, 70);
  for (int y = groundTop; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint32_t h = hash3(x, y, 21);
      rgb g = (h & 7) == 0 ? darken(ground, 40) : (h & 7) == 1 ? lighten(ground, 30) : ground;
      put(c, x, y, g);
    }
  if (place == AL_COAST || place == AL_LAKE) {
    int top = place == AL_COAST ? 27 : 34, bottom = place == AL_COAST ? 36 : 40;
    rgb water = mix(m.skyBottom, place == AL_COAST ? 0x2C6E9E : 0x3D6F8A, 150);
    if (grey) water = mix(water, overcast, grey / 2);
    if (season == 0 && place == AL_LAKE) water = mix(water, 0xE8F0F8, 140);  // frozen
    for (int y = top; y < bottom; y++)
      for (int x = 0; x < W; x++) {
        rgb w = y == top ? lighten(water, 40) : water;
        // Ripples move with the gusts; on still days they barely glint.
        if ((hash3((uint32_t)(x + drift / 2) / 3, (uint32_t)y, 31) % 9) == 0) w = lighten(w, 70);
        put(c, x, y, w);
      }
    if (place == AL_COAST)
      for (int x = 0; x < W; x++)
        if (((x + step) / 3) % 4 == 0) put(c, x, bottom, 0xF2F6F8);
  }
  if (place == AL_VILLAGE) {
    bool lit = night || activity > 0 || sky == AL_STORM || sky == AL_FOG;
    rgb wall = mix(0xE8D8B8, far, 60), roof = season == 0 ? 0xF4F8FC : 0x9E3B2A;
    house(c, 8, 34, wall, roof, lit, season == 0 || season == 3, step);
    house(c, 22, 33, wall, darken(roof, 30), lit && (step & 4), season == 0, step + 2);
    house(c, 55, 34, wall, roof, lit, season == 0, step + 4);
    house(c, 68, 33, wall, darken(roof, 50), lit, false, step);
  }

  // The season on the near ground.
  for (int i = 0; i < 26; i++) {
    int x = (int)(hash3(i, 41, month) % W), y = groundTop + 1 + (int)(hash3(i, 42, month) % (H - groundTop - 1));
    if (place == AL_LAKE && y < 40) continue;
    switch (season) {
      case 0: put(c, x, y, 0xC9D8EA); break;
      case 1: put(c, x, y, (i & 1) ? 0xFF9EC4 : 0xFFE36E); break;
      case 2: put(c, x, y, darken(ground, 80)), put(c, x, y - 1, darken(ground, 50)); break;
      default: put(c, x, y, (i % 3) == 0 ? 0xD9482A : (i % 3) == 1 ? 0xE89A2C : 0x8A4A22); break;
    }
  }

  // What falls or blows through the picture.
  const int amount = elements + weather;  // 0 .. 200
  if (sky == AL_RAIN || sky == AL_STORM) {
    int n = 8 + amount / 5 + (sky == AL_STORM ? 12 : 0);
    int slant = gusts / 40;
    for (int i = 0; i < n; i++) {
      int x = (int)(hash3(i, 51, 1) % (W + 10)) - (step * slant) % (W + 10);
      int y = (int)((hash3(i, 52, 1) % H + (uint32_t)step * 4) % H);
      x = (x % W + W) % W;
      blend(c, x, y, 0xDCE6F4, 230);
      blend(c, x - slant, y + 1, 0xC4D2E6, 190);
      blend(c, x - 2 * slant, y + 2, 0xC4D2E6, 110);
      if (y >= H - 2) put(c, x - 1, H - 1, 0xDDE6F2), put(c, x + 1, H - 1, 0xDDE6F2);
    }
  }
  if (sky == AL_SNOW) {
    int n = 10 + amount / 4;
    for (int i = 0; i < n; i++) {
      int y = (int)((hash3(i, 61, 1) % H + (uint32_t)step) % H);
      int sway = ((step + i) / 3) % 4;
      sway = sway < 2 ? sway : 4 - sway;
      int x = (int)((hash3(i, 62, 1) % W + (uint32_t)(sway + step * gusts / 60)) % W);
      put(c, x, y, 0xFFFFFF);
    }
  }
  if (sky == AL_BREEZE) {
    // Leaves in autumn, petals in spring, seeds in summer, flurries in winter, all carried sideways.
    rgb bits[4][2] = {{0xFFFFFF, 0xDDE6F2}, {0xFFB6D2, 0xFFFFFF}, {0xF4EEC8, 0xE0D49A}, {0xD9482A, 0xE89A2C}};
    int n = 3 + amount / 12;
    for (int i = 0; i < n; i++) {
      int speed = 2 + gusts / 25 + (int)(hash3(i, 71, 1) % 2);
      int x = (int)((hash3(i, 72, 1) % W + (uint32_t)(step * speed)) % W);
      int y0 = (int)(hash3(i, 73, 1) % 34);
      int y = y0 + ((step + i) / 2) % 6;
      put(c, x, y, bits[season][i & 1]);
      if (season == 3) put(c, x + 1, y, bits[season][(i + 1) & 1]);
    }
  }
  if (sky == AL_FOG) {
    // Bands of mist, dithered, drifting slowly.
    int thick = 40 + weather * 4 / 5;
    for (int band = 0; band < 4; band++) {
      int y0 = 16 + band * 6, off = (step * (band + 1)) / 2;
      for (int y = y0; y < y0 + 4; y++)
        for (int x = 0; x < W; x++)
          if (((x + y + off) & 1) == 0 || ((x / 7 + off / 3 + band) % 3) != 0) blend(c, x, y, 0xD8DCDF, thick);
    }
  }
  if ((sky == AL_CLEAR || sky == AL_BREEZE) && !night && season != 0 && s[AL_MOOD] > 40) {
    // Birds on a fine day.
    for (int i = 0; i < 2; i++) {
      int x = (int)((hash3(i, 81, 1) % W + (uint32_t)step) % (W + 8)) - 4;
      int y = 10 + i * 4 + ((step / 2 + i) & 1);
      bool up = ((step + i) & 1) != 0;
      put(c, x - 1, y - (up ? 1 : 0), 0x2E3440), put(c, x, y, 0x2E3440), put(c, x + 1, y - (up ? 1 : 0), 0x2E3440);
    }
  }

  // Notes rise from the ground as a few motes in the month's colour.
  if (activity > 0) {
    int n = 1 + activity / 20;
    for (int i = 0; i < n; i++) {
      int x = (int)(hash3(i, (uint32_t)step / 6, 91) % W);
      int y = 36 - (step % 6) * 3 - (int)(hash3(i, 92, 1) % 4);
      put(c, x, y, lighten(m.accent, 110));
    }
  }

  // The mood grades the whole picture: cooler and greyer when wistful, warmer when glad.
  int mood = s[AL_MOOD] - 50;
  if (mood != 0)
    for (int i = 0; i < W * H; i++) {
      rgb p = c->px[i];
      if (mood < 0) {
        int luma = (int)(((p >> 16) & 255) * 77 + ((p >> 8) & 255) * 150 + (p & 255) * 29) >> 8;
        rgb cool = (rgb)((luma * 7 / 8) << 16 | (luma * 15 / 16) << 8 | clampi(luma + 16, 0, 255));
        c->px[i] = mix(p, cool, -mood * 3);
      } else {
        c->px[i] = mix(p, 0xFFD9A0, mood);
      }
    }
}
