// The pool and the lanes, in plain C with integer arithmetic, so a firmware can run them as they are.
//
// Time is counted in ticks from the clock's start, AP_PPQ to a beat. A lane's steps sit at whole multiples of its
// rate, and the n-th strike of a lane plays the n-th note of its shape, so where a lane is depends on the elapsed
// ticks and the pool alone. A new note in the pool changes the pattern from the next strike on. It uses no C
// library, so a firmware can build this same file.
#include "ArpeggiCore.h"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Division and remainder rounded down, for times before the start. Out of line, so a constant divisor never becomes
// a 64-bit multiply by its reciprocal (see below).
__attribute__((noinline)) static int64_t fdiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  return (a % b != 0 && a < 0) ? q - 1 : q;
}

__attribute__((noinline)) static int64_t fmod64(int64_t a, int64_t b) {
  int64_t r = a % b;
  return r < 0 ? r + b : r;
}

// Every 64-bit product is made here, out of line: on the FM-1's CPU a 64-bit multiply next to a memory load can
// corrupt a register (felucca-bsp's docs/CPU.md).
__attribute__((noinline)) static int64_t mul(int64_t a, int64_t b) { return a * b; }

// A well-mixed 32-bit hash, so random shapes and rhythms come out the same every time round.
static uint32_t hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

// ---------------------------------------------------------------- the pool

static int find(const int8_t* a, int n, int note) {
  for (int i = 0; i < n; i++)
    if (a[i] == note) return i;
  return -1;
}

static void remove_at(int8_t* a, int8_t* n, int i) {
  for (int j = i; j + 1 < *n; j++) a[j] = a[j + 1];
  (*n)--;
}

void ap_pool_clear(ApPool* p) {
  p->count = 0;
  p->next = 0;
}

void ap_pool_init(ApPool* p, int hold, int ring) {
  for (int i = 0; i < AP_MAX_POOL; i++) p->note[i] = -1, p->held[i] = -1;
  p->count = 0, p->held_count = 0, p->next = 0;
  p->hold = (int8_t)clampi(hold, 0, AP_HOLD_COUNT - 1);
  p->ring = (int8_t)clampi(ring, 1, 8);
}

// The pool oldest first. A full ring's oldest note is the one the next key replaces.
static int age_order(const ApPool* p, int8_t out[AP_MAX_POOL]) {
  int start = (p->hold == AP_RING && p->count == p->ring) ? p->next : 0;
  for (int i = 0; i < p->count; i++) out[i] = p->note[(start + i) % p->count];
  return p->count;
}

void ap_pool_mode(ApPool* p, int hold, int ring) {
  hold = clampi(hold, 0, AP_HOLD_COUNT - 1);
  ring = clampi(ring, 1, 8);
  int8_t aged[AP_MAX_POOL];
  int n = age_order(p, aged);
  p->hold = (int8_t)hold, p->ring = (int8_t)ring;
  if (hold == AP_HOLD) {
    // Only what is held down now.
    int k = 0;
    for (int i = 0; i < n; i++)
      if (find(p->held, p->held_count, aged[i]) >= 0) aged[k++] = aged[i];
    n = k;
  }
  int first = 0;
  if (hold == AP_RING && n > ring) first = n - ring;
  p->count = 0;
  for (int i = first; i < n; i++) p->note[p->count++] = aged[i];
  p->next = 0;
}

static void ring_put(ApPool* p, int note) {
  if (p->count < p->ring) {
    p->note[p->count++] = (int8_t)note;
    p->next = 0;
  } else {
    p->note[p->next] = (int8_t)note;
    p->next = (int8_t)((p->next + 1) % p->ring);
  }
}

bool ap_pool_key_down(ApPool* p, int note) {
  if (note < 0 || note > 127) return false;
  const bool was_empty = p->count == 0;
  const bool first_key = p->held_count == 0;
  if (find(p->held, p->held_count, note) < 0 && p->held_count < AP_MAX_POOL) p->held[p->held_count++] = (int8_t)note;
  if (p->hold == AP_RING) {
    ring_put(p, note);
  } else {
    // In Hold and Latch, the first key of a new chord clears what was there, a latched chord or a seed.
    if (first_key) ap_pool_clear(p);
    if (find(p->note, p->count, note) < 0 && p->count < AP_MAX_POOL) p->note[p->count++] = (int8_t)note;
  }
  return was_empty && p->count > 0;
}

void ap_pool_key_up(ApPool* p, int note) {
  int i = find(p->held, p->held_count, note);
  if (i >= 0) remove_at(p->held, &p->held_count, i);
  if (p->hold == AP_HOLD) {
    int j = find(p->note, p->count, note);
    if (j >= 0) remove_at(p->note, &p->count, j);
  }
}

void ap_pool_seed(ApPool* p, const int8_t* notes, int n) {
  ap_pool_clear(p);
  int first = 0;
  if (p->hold == AP_RING && n > p->ring) first = n - p->ring;
  for (int i = first; i < n && p->count < AP_MAX_POOL; i++)
    if (notes[i] >= 0) p->note[p->count++] = notes[i];
  p->next = 0;
}

int ap_pool_sorted(const ApPool* p, int8_t out[AP_MAX_POOL]) {
  int n = p->count;
  for (int i = 0; i < n; i++) {
    int8_t v = p->note[i];
    int j = i;
    while (j > 0 && out[j - 1] > v) {
      out[j] = out[j - 1];
      j--;
    }
    out[j] = v;
  }
  return n;
}

// ---------------------------------------------------------------- settings

static const int kRateTicks[AP_RATE_COUNT] = {
    2 * AP_PPQ,     AP_PPQ,         AP_PPQ * 3 / 2, AP_PPQ / 2,     AP_PPQ * 3 / 4, AP_PPQ / 3,
    AP_PPQ / 4,     AP_PPQ * 3 / 8, AP_PPQ / 6,     AP_PPQ / 5,     AP_PPQ / 7,     AP_PPQ / 8};

int ap_rate_ticks(int rate) { return kRateTicks[clampi(rate, 0, AP_RATE_COUNT - 1)]; }

void ap_proc_make(const int32_t s[AP_FIELDS], ApProc* out) {
  out->relation = clampi(s[AP_RELATION], 0, AP_RELATION_COUNT - 1);
  out->duo = s[AP_DUO] ? 1 : 0;
  for (int l = 0; l < 2; l++) {
    const int base = l ? AP_B_SHAPE : AP_A_SHAPE;
    out->shape[l] = clampi(s[base], 0, AP_SHAPE_COUNT - 1);
    out->rate[l] = clampi(s[base + 1], 0, AP_RATE_COUNT - 1);
    out->rhythm[l] = clampi(s[base + 2], 0, AP_RHYTHM_COUNT - 1);
    out->gate[l] = clampi(s[base + 3], 5, 200);
  }
  out->span = clampi(s[AP_SPAN], 1, AP_MAX_SPAN);
  out->pitch = clampi(s[AP_B_PITCH], -24, 24);
  out->swing = clampi(s[AP_SWING], 0, 100);
  out->tempo = clampi(s[AP_TEMPO], 40, 240);
  out->transpose = 0;
}

static const int64_t kBar = 4 * (int64_t)AP_PPQ;

int ap_shape_at(const ApProc* p, int lane, int64_t tick) {
  if (p->duo && p->relation == AP_TRADE && fmod64(fdiv(tick, 2 * kBar), 2) == 1) lane ^= 1;
  return p->shape[lane & 1];
}

// ---------------------------------------------------------------- strikes

// When each of the bouncing ball's 12 strikes falls in its 16 steps, in thousandths: each gap 0.8 of the one
// before, (1 - 0.8^i) / (1 - 0.8^12).
static const int kFall[12] = {0, 215, 387, 524, 634, 722, 792, 849, 894, 930, 958, 982};
#define FALL_STEPS 16

// A strike within a step: how far into the step, its count on the lane, and the time to the next strike.
typedef struct {
  int64_t offset, h;
  int64_t spacing;
} Strike;

static bool euclid_hit(int64_t i, int hits, int steps) { return fmod64(mul(i, hits), steps) < hits; }

static int euclid_before(int64_t i, int hits, int steps) {
  int n = 0;
  for (int j = 0; j < i; j++) n += euclid_hit(j, hits, steps);
  return n;
}

static bool skip_hit(int i) { return i == 0 || hash(0x5eed0000u + (uint32_t)i) % 3 != 0; }

// The strikes of step k (k >= 0) in a rhythm, at a rate of r ticks a step. Returns how many, at most 3.
static int step_strikes(int rhythm, int64_t r, int64_t k, Strike out[3]) {
  switch (rhythm) {
    case AP_E7:
    case AP_E5:
    case AP_E3:
    case AP_E5_12: {
      const int hits = rhythm == AP_E7 ? 7 : rhythm == AP_E3 ? 3 : 5;
      const int steps = rhythm == AP_E5_12 ? 12 : 8;
      const int64_t i = fmod64(k, steps);
      if (!euclid_hit(i, hits, steps)) return 0;
      out[0].offset = 0, out[0].spacing = r;
      out[0].h = mul(fdiv(k, steps), hits) + euclid_before(i, hits, steps);
      return 1;
    }
    case AP_RATCHET:
      if (fmod64(k, 4) == 3) {
        for (int j = 0; j < 3; j++) out[j].offset = fdiv(mul(r, j), 3), out[j].h = k, out[j].spacing = fdiv(r, 3);
        return 3;
      }
      out[0].offset = 0, out[0].h = k, out[0].spacing = r;
      return 1;
    case AP_GALLOP:
      if (fmod64(k, 2) == 0) {
        out[0].offset = 0, out[0].h = mul(fdiv(k, 2), 3), out[0].spacing = r;
        return 1;
      }
      for (int j = 0; j < 2; j++) out[j].offset = fdiv(mul(r, j), 2), out[j].h = mul(fdiv(k, 2), 3) + 1 + j, out[j].spacing = fdiv(r, 2);
      return 2;
    case AP_SKIP: {
      const int i = (int)fmod64(k, 16);
      if (!skip_hit(i)) return 0;
      int per = 0, before = 0;
      for (int j = 0; j < 16; j++) {
        if (j == i) before = per;
        per += skip_hit(j);
      }
      out[0].offset = 0, out[0].spacing = r, out[0].h = mul(fdiv(k, 16), per) + before;
      return 1;
    }
    case AP_FALL: {
      const int64_t cycle = mul(r, FALL_STEPS), c = fdiv(k, FALL_STEPS);
      const int i = (int)fmod64(k, FALL_STEPS);
      int n = 0;
      for (int j = 0; j < 12 && n < 3; j++) {
        if (kFall[j] * FALL_STEPS / 1000 != i) continue;
        const int next = j + 1 < 12 ? kFall[j + 1] : 1000;
        out[n].offset = fdiv(mul(cycle, kFall[j]), 1000) - mul(i, r);
        out[n].h = mul(c, 12) + j;
        out[n].spacing = fdiv(mul(cycle, next - kFall[j]), 1000);
        n++;
      }
      return n;
    }
    default:
      out[0].offset = 0, out[0].h = k, out[0].spacing = r;
      return 1;
  }
}

// ---------------------------------------------------------------- shapes

// A strum's notes are this far apart: about 20 ms at 120 BPM.
#define STRUM_TICKS (AP_PPQ / 48)

typedef struct {
  int8_t list[AP_MAX_POOL * AP_MAX_SPAN];  // the pool through every octave of the span
  int len, n;                              // len = n * span
} Expanded;

static void expand(const ApPool* pool, bool ordered, int span, Expanded* e) {
  int8_t base[AP_MAX_POOL];
  if (ordered) {
    e->n = pool->count;
    for (int i = 0; i < e->n; i++) base[i] = pool->note[i];
  } else {
    e->n = ap_pool_sorted(pool, base);
  }
  e->len = e->n * span;
  for (int o = 0, k = 0; o < span; o++)
    for (int i = 0; i < e->n; i++) e->list[k++] = (int8_t)(base[i] + 12 * o);
}

// Where a single-note shape is on its h-th strike, as an index into the expanded pool.
static int shape_index(int shape, const Expanded* e, int64_t h) {
  const int L = e->len;
  if (L <= 1) return 0;
  const int m = (int)fmod64(h, L);
  switch (shape) {
    case AP_DOWN: return L - 1 - m;
    case AP_BOUNCE: {
      const int period = 2 * L - 2, b = (int)fmod64(h, period);
      return b < L ? b : period - b;
    }
    case AP_CONVERGE: return (m & 1) ? L - 1 - m / 2 : m / 2;
    case AP_THUMB: {
      const int period = 2 * (L - 1), t = (int)fmod64(h, period);
      return (t & 1) ? 1 + t / 2 : 0;
    }
    case AP_SPIRAL: return (int)fmod64(m + fdiv(h, L), L);
    case AP_WALK: {
      // Sixteen strikes a walk, each starting somewhere new, a step up or down at a time and turning at the ends.
      const int64_t c = fdiv(h, 16);
      const int steps = (int)fmod64(h, 16);
      int pos = (int)(hash(0xa11cu ^ (uint32_t)c) % (uint32_t)L);
      for (int j = 1; j <= steps; j++) {
        pos += (hash(((uint32_t)c << 4) + (uint32_t)j) & 1) ? 1 : -1;
        if (pos < 0) pos = 1;
        if (pos >= L) pos = L - 2;
      }
      return pos;
    }
    case AP_SHUFFLE: {
      int8_t perm[AP_MAX_POOL * AP_MAX_SPAN];
      for (int i = 0; i < L; i++) perm[i] = (int8_t)i;
      uint32_t seed = hash(0x5u ^ (uint32_t)fdiv(h, L));
      for (int i = L - 1; i > 0; i--) {
        seed = hash(seed + (uint32_t)i);
        int j = (int)(seed % (uint32_t)(i + 1));
        int8_t t = perm[i];
        perm[i] = perm[j], perm[j] = t;
      }
      return perm[m];
    }
    default: return m;  // Up, Order
  }
}

// The notes a shape plays on its h-th strike, with each one's delay in ticks (a strum's spread). Mirrored turns
// the shape upside down. Returns how many.
static int shape_notes(int shape, const ApPool* pool, int span, int64_t h, bool mirrored, int8_t notes[AP_MAX_POOL],
                       int delay[AP_MAX_POOL]) {
  Expanded e;
  expand(pool, shape == AP_ORDER, span, &e);
  if (e.n == 0) return 0;
  if (shape == AP_CHORD || shape == AP_STRUM) {
    const int octave = shape == AP_CHORD ? 12 * (int)fmod64(h, span) : 0;
    const bool down = (shape == AP_STRUM && fmod64(h, 2) == 1) != mirrored;
    for (int i = 0; i < e.n; i++) {
      notes[i] = (int8_t)clampi(e.list[i] + octave, 0, 127);
      delay[i] = shape == AP_STRUM ? STRUM_TICKS * (down ? e.n - 1 - i : i) : 0;
    }
    return e.n;
  }
  int idx = shape_index(shape, &e, h);
  if (mirrored) idx = e.len - 1 - idx;
  notes[0] = e.list[idx];
  delay[0] = 0;
  return 1;
}

// ---------------------------------------------------------------- the lanes

// What one lane plays from: whose steps it strikes on (its own, or A's in Hocket and Canon), how far behind, which
// strikes are its own, and its shape and pitch.
typedef struct {
  int lane;
  int64_t rate;
  int rhythm;
  int64_t shift;
  int parity;  // Hocket: 0 for A's strikes, 1 for B's, -1 for every strike
  int pitch;
  bool mirrored;
} Source;

static Source source(const ApProc* p, int lane) {
  Source s;
  s.lane = lane;
  s.parity = -1;
  s.shift = 0;
  s.pitch = lane ? p->pitch : 0;
  s.mirrored = false;
  int timing = lane;
  if (p->duo) {
    if (p->relation == AP_HOCKET) s.parity = lane, timing = 0;
    if (p->relation == AP_CANON && lane == 1) timing = 0, s.shift = mul(3, ap_rate_ticks(p->rate[0]));
    if (p->relation == AP_MIRROR && lane == 1) s.mirrored = true;
  }
  s.rate = ap_rate_ticks(p->rate[timing]);
  s.rhythm = p->rhythm[timing];
  return s;
}

// The shape a strike plays: a lane's own, except where B borrows A's (Canon, Mirror).
static int strike_shape(const ApProc* p, const Source* s, int64_t tick) {
  if (s->lane == 1 && p->duo && (p->relation == AP_CANON || p->relation == AP_MIRROR)) return ap_shape_at(p, 0, tick);
  return ap_shape_at(p, s->lane, tick);
}

int ap_next_notes(const ApProc* p, const ApPool* pool, int64_t from, int64_t to, ApNote out[AP_MAX_OUT]) {
  if (pool->count == 0 || to <= from) return 0;
  int64_t best = to;
  int count = 0;
  const int lanes = p->duo ? 2 : 1;
  for (int lane = 0; lane < lanes; lane++) {
    const Source s = source(p, lane);
    const int64_t r = s.rate;
    // A strike can land later than its step by swing, a strum and a canon's shift, so start that far back.
    const int64_t back = s.shift + STRUM_TICKS * AP_MAX_POOL + r;
    int64_t k = fdiv(from - back, r);
    if (k < 0) k = 0;
    for (;; k++) {
      int64_t base = mul(k, r) + s.shift;
      if (base > best || base >= to) break;
      if (s.rhythm != AP_FALL && fmod64(k, 2) == 1) base += fdiv(mul(r, p->swing), 300);
      Strike st[3];
      const int n = step_strikes(s.rhythm, r, k, st);
      for (int j = 0; j < n; j++) {
        const int64_t at = base + st[j].offset;
        int64_t h = st[j].h;
        if (s.parity >= 0) {
          if ((int)fmod64(h, 2) != s.parity) continue;
          h = fdiv(h, 2);
        }
        if (p->duo && p->relation == AP_ANSWER && (int)fmod64(fdiv(at, kBar), 2) != lane) continue;
        const int64_t shapeTick = at - s.shift;
        int8_t notes[AP_MAX_POOL];
        int delay[AP_MAX_POOL];
        const int m = shape_notes(strike_shape(p, &s, shapeTick), pool, p->span, h, s.mirrored, notes, delay);
        int64_t length = fdiv(mul(st[j].spacing, p->gate[lane]), 100);
        if (length < 1) length = 1;
        for (int i = 0; i < m; i++) {
          const int64_t t = at + delay[i];
          const int note = notes[i] + p->transpose + s.pitch;
          if (t < from || t > best || note < 0 || note > 127) continue;
          if (t < best) best = t, count = 0;
          if (count < AP_MAX_OUT) {
            ApNote* o = &out[count++];
            o->tick = t, o->lane = lane, o->strike = (int)h, o->note = note;
            o->length = (int32_t)(length > 0x7fffffff ? 0x7fffffff : length);
          }
        }
      }
    }
  }
  return best < to ? count : 0;
}
