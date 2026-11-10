// The modal piano, in plain C with no C library, so a firmware can build this same file.
//
// A struck string rings as a set of partials, each at n f0 sqrt(1 + B n^2) for its inharmonicity B. Two or three
// strings tuned a hair apart share each key; their coupling makes every partial die away in two stages, fast and
// then slow, with a beat. Each partial is therefore two oscillators, both started together: a fast-decaying one at
// its frequency and a slow one a beat away. Every oscillator is a complex number turned and shrunk once a sample.
#include "piano.h"

typedef union {
  float f;
  uint32_t u;
} pn_bits;

#include "piano_tables.inc"

static float pn_exp2(float x) {
  if (x < -126.0f) return 0.0f;
  if (x > 126.0f) x = 126.0f;
  float rf = x >= 0 ? (float)(int32_t)(x + 0.5f) : -(float)(int32_t)(-x + 0.5f);
  float f = x - rf;  // within half of 0
  float p = 1.0f + f * (0.6931471806f + f * (0.2402265070f + f * (0.0555041087f +
            f * (0.0096181291f + f * (0.0013333558f + f * 0.0001540353f)))));
  pn_bits b;
  b.u = (uint32_t)((int32_t)rf + 127) << 23;
  return p * b.f;
}

static float pn_db(float db) { return pn_exp2(db * 0.16609640474f); }

static float pn_log2(float x) {
  pn_bits b;
  b.f = x;
  int e = (int)((b.u >> 23) & 0xFF) - 127;
  b.u = (b.u & 0x007FFFFFu) | 0x3F800000u;  // the mantissa, 1 to 2
  float m = b.f;
  if (m > 1.41421356f) {
    m *= 0.5f;
    e++;
  }
  float t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
  float ln = 2.0f * t * (1.0f + t2 * (0.33333333f + t2 * (0.2f + t2 * (0.14285714f + t2 * 0.11111111f))));
  return (float)e + ln * 1.44269504f;
}

static float pn_sqrt(float x) {
  if (x <= 0) return 0;
  pn_bits b;
  b.f = x;
  b.u = (b.u >> 1) + 0x1FC00000u;
  float y = b.f;
  for (int i = 0; i < 4; i++) y = 0.5f * (y + x / y);
  return y;
}

// sin and cos of x in [0, pi].
static void pn_sincos(float x, float* s, float* c) {
  const float PI = 3.14159265358979f;
  float sgn_c = 1.0f;
  if (x > 0.5f * PI) {
    x = PI - x;
    sgn_c = -1.0f;
  }
  int swap = x > 0.25f * PI;
  if (swap) x = 0.5f * PI - x;
  float x2 = x * x;
  float sn = x * (1.0f - x2 * (1.0f / 6 - x2 * (1.0f / 120 - x2 * (1.0f / 5040 - x2 * (1.0f / 362880)))));
  float cs = 1.0f - x2 * (0.5f - x2 * (1.0f / 24 - x2 * (1.0f / 720 - x2 * (1.0f / 40320 - x2 * (1.0f / 3628800)))));
  if (swap) {
    float t = sn;
    sn = cs;
    cs = t;
  }
  *s = sn;
  *c = cs * sgn_c;
}

// How fast a partial grows at the strike, nepers a second: a time constant of 8.5 ms up to 300 Hz, falling to 2 ms by
// 2 kHz, as measured in the recordings at ff.
static float rise_rate(float f) {
  float t = f <= 300.0f ? 0.0f : f >= 2000.0f ? 1.0f : pn_log2(f * (1.0f / 300.0f)) * (1.0f / 2.737f);
  return 1000.0f / (8.5f - 6.5f * t);
}

// Strings above this key have no damper.
#define PN_TOP_DAMPER 89

// How fast a damper stops a string, nepers a second: bass dampers grip less (inferred, not measured).
static float damper_rate(int note) {
  float t60 = note < 40 ? 0.8f - (note - 21) * 0.013f : note < 70 ? 0.55f - (note - 40) * 0.009f : 0.28f;
  return 6.9078f / t60;
}

static uint32_t rnd(pn_piano_t* p) {
  p->noise = p->noise * 1664525u + 1013904223u;
  return p->noise;
}

// Each soundboard mode's level at ff, dB, from 55 Hz up to 2.4 kHz, fitted to the knock in the recordings.
static const float kBoardDb[PN_BOARD] = {
    -18.4f, -29.5f, -19.5f, -3.9f, 0.2f, 2.3f, 2.1f, 2.0f, 2.3f, 1.8f, 1.1f, 1.0f, 3.8f, 10.0f, 16.2f, 14.7f,
    11.0f, 12.1f, 12.9f, 12.3f, 10.8f, 8.1f, 6.0f, 4.3f, 3.4f, 5.8f, 8.6f, 9.7f, 10.5f, 10.2f, 8.1f, 2.9f,
};

// The board's modes: spread evenly in log frequency with a fixed scatter, each a two-pole resonator decaying as the
// recordings' knock does, slowly in the low modes and fast in the high.
static void board_setup(pn_piano_t* p) {
  uint32_t h = 0x9E3779B9u;
  for (int m = 0; m < PN_BOARD; m++) {
    h = h * 1664525u + 1013904223u;
    float jitter = ((float)(h >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.16f;
    float f = 55.0f * pn_exp2(((float)m + jitter * 3.0f) * (5.45f / (PN_BOARD - 1)));
    float oct = pn_log2(f * (1.0f / 150.0f)) / 3.7f;
    float k = 10.0f + 20.0f * (oct < 0 ? 0 : oct > 1 ? 1 : oct);
    float r = pn_exp2(-k / p->sr * 1.44269504f), sn, cs;
    pn_sincos(6.28318531f * f / p->sr, &sn, &cs);
    p->board_f[m] = f;
    p->board_c1[m] = 2 * r * cs;
    p->board_c2[m] = r * r;
    p->board_b[m] = 2 * (1 - r) * sn;  // unity at the resonance
    p->board_db[m] = kBoardDb[m];
  }
}

void pn_init(pn_piano_t* p, float sr) {
  uint8_t* b = (uint8_t*)p;
  for (unsigned i = 0; i < sizeof *p; i++) b[i] = 0;
  p->sr = sr;
  p->budget = PN_MODES;
  p->gain = 0.5f;
  p->cull2 = 1e-11f;  // about -110 dB of one partial at full scale
  p->noise = 0x1234567u;
  for (int n = 0; n < PN_NOTES; n++) p->damped[n] = 1;
  board_setup(p);
  p->knock_db = 0;
  p->knock_slope = 0.25f;
  p->knock_vel = 1.3f;
  p->knock_tilt = 0.5f;
  p->decay = p->beat = p->knock = p->stretch = p->release = 1.0f;
  p->soft_db = 12;
}

// Room for a note's oscillators: free slots first, then the quietest, if quieter than the new ones.
static int take_slot(pn_piano_t* p, float mag2) {
  if (p->count < p->budget) return p->count++;
  int w = -1;
  float m = mag2;
  for (int i = 0; i < p->count; i++)
    if (p->mag2[i] < m) {
      m = p->mag2[i];
      w = i;
    }
  return w;
}

static void set_mode(pn_piano_t* p, int i, int note, float amp, float f, float k, float krel, int damped) {
  float s, c;
  pn_sincos(6.28318531f * f / p->sr, &s, &c);
  float r0 = pn_exp2(-k / p->sr * 1.44269504f);
  float rd = krel > k ? pn_exp2(-krel / p->sr * 1.44269504f) : r0;
  float r = damped ? rd : r0;
  p->zr[i] = amp;  // Im(z) starts at 0: every partial starts as a sine, from rest
  p->zi[i] = 0;
  p->wr[i] = r * c;
  p->wi[i] = r * s;
  p->r0[i] = r0;
  p->rd[i] = rd;
  p->mag2[i] = amp * amp;
  p->note[i] = (uint8_t)note;
}

static void note_params(int note, int i, float* w0, int* i0, int* i1) {
  float t = (float)(note - PN_FIRST_MIDI) / PN_STEP;
  if (t < 0) t = 0;
  if (t > PN_TABLE_NOTES - 1) t = PN_TABLE_NOTES - 1;
  int a = (int)t;
  if (a > PN_TABLE_NOTES - 2) a = PN_TABLE_NOTES - 2;
  *i0 = a;
  *i1 = a + 1;
  *w0 = 1.0f - (t - a);
  (void)i;
}

// One table note's partial n (from 1): its values, or, past its last measured partial, the last one fading.
static void partial_at(int tn, int n, pn_partial_t* out) {
  const pn_note_t* nt = &pn_notes[tn];
  if (n <= nt->count) {
    *out = pn_partials[nt->first + n - 1];
    return;
  }
  *out = pn_partials[nt->first + nt->count - 1];
  out->lvl -= 6.0f * (float)(n - nt->count);
}

void pn_note_on(pn_piano_t* p, int note, int vel) {
  if (note < 0 || note >= PN_NOTES || vel <= 0) return;
  float w0;
  int i0, i1;
  note_params(note, 0, &w0, &i0, &i1);
  const pn_note_t *A = &pn_notes[i0], *Bn = &pn_notes[i1];
  float B = w0 * A->B + (1 - w0) * Bn->B;
  float cents = (w0 * A->cents + (1 - w0) * Bn->cents) * p->stretch;
  float f0 = 440.0f * pn_exp2((note - 69) / 12.0f + cents / 1200.0f);

  // Loudness and colour: pp at velocity 24, mf at 64, ff at 110, carried on past both ends.
  float c0, c1;
  float mf0 = w0 * A->mf_c0 + (1 - w0) * Bn->mf_c0, mf1 = w0 * A->mf_c1 + (1 - w0) * Bn->mf_c1;
  float pp0 = w0 * A->pp_c0 + (1 - w0) * Bn->pp_c0, pp1 = w0 * A->pp_c1 + (1 - w0) * Bn->pp_c1;
  if (pp1 > 1.6f * mf1) pp1 = 1.6f * mf1;  // softer is never brighter
  if (vel >= 64) {
    float u = (vel - 110) / 46.0f;      // -1 at mf, 0 at ff
    float k = u < 0 ? -u : -0.5f * u;  // past ff, louder and brighter at half the rate
    c0 = k * mf0;
    c1 = k * mf1;
  } else {
    float u = (vel - 24) / 40.0f;  // 0 at pp, 1 at mf
    c0 = pp0 + u * (mf0 - pp0);
    c1 = pp1 + u * (mf1 - pp1);
  }

  // How much slower a softer blow lets the partials grow: the hammer stays on the string longer.
  float rise = vel >= 110 ? 1.0f : 1.0f / (1.0f + 0.5f * (110 - vel) / 86.0f);
  // dB per octave above 1.5 kHz that a blow softer than ff loses (fitted to the recordings at mf).
  float soft = vel < 110 ? p->soft_db * (110 - vel) / 46.0f : 0;
  int damped = 0;  // the key lifts its damper
  float krel = note < PN_TOP_DAMPER ? damper_rate(note) * p->release : 0;
  int nmax = A->count > Bn->count ? A->count : Bn->count;
  float fmax = p->sr * 0.45f;
  if (fmax > 18000.0f) fmax = 18000.0f;

  // The strike on the board: the oldest burst gives way. A softer blow touches the string longer, 2 to 4 ms.
  {
    int b = 0;
    for (int i = 0; i < PN_STRIKES; i++) {
      if (p->strike[i].t >= p->strike[i].len) {
        b = i;
        break;
      }
      if (p->strike[i].t > p->strike[b].t) b = i;
    }
    float hard = vel > 110 ? 1.0f : vel < 24 ? 0.0f : (vel - 24) / 86.0f;
    p->strike[b].len = (int)(p->sr * (0.004f - 0.002f * hard));
    p->strike[b].t = 0;
    float kn = (note - 60) * p->knock_slope + p->knock_vel * c0;
    uint32_t h = (uint32_t)note * 2654435761u;
    for (int m = 0; m < PN_BOARD; m++) {
      h = h * 1664525u + 1013904223u;
      float own = ((float)(h >> 8) * (1.0f / 16777216.0f) - 0.5f) * 6.0f;  // this key's place on the bridge
      float f = p->board_f[m];
      float db = p->knock_db + p->board_db[m] + kn + own;
      if (f > 300.0f) db += (c1 + p->tilt) * p->knock_tilt * pn_log2(f * (1.0f / 300.0f));
      if (soft > 0 && f > 1500.0f) db -= soft * pn_log2(f * (1.0f / 1500.0f));
      p->strike[b].g[m] = pn_db(db) * p->gain * p->knock;
    }
    p->board_live = (int)p->sr;
  }
  if (p->strings_off) return;

  pn_cand_t* c = p->cand;
  int nc = 0;
  for (int n = 1; n <= nmax && n <= 128; n++) {
    float fn = n * f0 * pn_sqrt(1.0f + B * n * n);
    if (fn >= fmax) break;
    pn_partial_t pa, pb;
    partial_at(i0, n, &pa);
    partial_at(i1, n, &pb);
    float lvl = w0 * pa.lvl + (1 - w0) * pb.lvl + c0 + (c1 + p->tilt) * pn_log2((float)n);
    // A softer blow leaves the felt soft, and it loses the top far faster than the low partials show.
    if (soft > 0 && fn > 1500.0f) lvl -= soft * pn_log2(fn * (1.0f / 1500.0f));
    float d2 = w0 * pa.d2 + (1 - w0) * pb.d2;
    float k1 = (w0 * pa.k1 + (1 - w0) * pb.k1) * p->decay, k2 = (w0 * pa.k2 + (1 - w0) * pb.k2) * p->decay;
    float beat = (w0 * pa.beat + (1 - w0) * pb.beat) * p->beat;
    float a1 = pn_db(lvl) * p->gain, a2 = pn_db(lvl + d2) * p->gain;
    float kr = krel * (1.0f + fn * (1.0f / 4000.0f));
    if (a1 * a1 > p->cull2 * 4) c[nc++] = (pn_cand_t){a1, fn, k1, kr};
    if (a2 * a2 > p->cull2 * 4) c[nc++] = (pn_cand_t){a2, fn + ((n & 1) ? -beat : beat), k2, kr};
    // The rise: a partial grows while the hammer is on the string, in about 8 ms in the bass and 2 ms at the top
    // (measured), slower from a softer blow. An oscillator of the opposite sign, dying at that rate, cancels the
    // partial at the strike and lets it through as it fades.
    if (a1 * a1 > p->cull2 * 4) c[nc++] = (pn_cand_t){-(a1 + a2), fn, rise * rise_rate(fn), kr};
  }
  // The bass's peaks off the harmonic series, from the nearest measured key. They come from the string's stretching,
  // which grows as the square of the swing, so they grow twice as fast as the partials with velocity (inferred).
  {
    int tn = w0 >= 0.5f ? i0 : i1;
    const pn_note_t* T = &pn_notes[tn];
    float scale = pn_sqrt(pn_exp2((note - (PN_FIRST_MIDI + tn * PN_STEP)) / 12.0f));
    for (int x = 0; x < T->xcount && nc < PN_CANDS - 1; x++) {
      const pn_extra_t* e = &pn_extras[T->xfirst + x];
      float fx = e->f * scale;
      if (fx >= fmax) continue;
      float a1 = pn_db(e->lvl + 2 * c0) * p->gain, a2 = pn_db(e->lvl + e->d2 + 2 * c0) * p->gain;
      float kr = krel * (1.0f + fx * (1.0f / 4000.0f));
      if (a1 * a1 > p->cull2 * 4) c[nc++] = (pn_cand_t){a1, fx, e->k1 * p->decay, kr};
      if (a2 * a2 > p->cull2 * 4) c[nc++] = (pn_cand_t){a2, fx, e->k2 * p->decay, kr};
    }
  }
  // Loudest first, so a full pool keeps the partials that matter.
  for (int i = 1; i < nc; i++) {
    pn_cand_t t = c[i];
    int j = i - 1;
    while (j >= 0 && c[j].a * c[j].a < t.a * t.a) {
      c[j + 1] = c[j];
      j--;
    }
    c[j + 1] = t;
  }
  for (int i = 0; i < nc; i++) {
    int s = take_slot(p, c[i].a * c[i].a);
    if (s < 0) break;
    set_mode(p, s, note, c[i].a, c[i].f, c[i].k, c[i].krel, damped);
  }
  p->held[note] = 1;
  p->damped[note] = (uint8_t)damped;
}

// The damper lands on a note's strings, or lifts.
static void damp(pn_piano_t* p, int note, int on) {
  if (note >= PN_TOP_DAMPER || p->damped[note] == on) return;
  p->damped[note] = (uint8_t)on;
  for (int i = 0; i < p->count; i++) {
    if (p->note[i] != note) continue;
    float s = on ? p->rd[i] / p->r0[i] : p->r0[i] / p->rd[i];
    p->wr[i] *= s;
    p->wi[i] *= s;
  }
}

void pn_note_off(pn_piano_t* p, int note) {
  if (note < 0 || note >= PN_NOTES) return;
  p->held[note] = 0;
  if (!p->pedal) damp(p, note, 1);
}

void pn_pedal(pn_piano_t* p, int down) {
  p->pedal = (uint8_t)(down != 0);
  for (int n = 0; n < PN_NOTES; n++) damp(p, n, !p->pedal && !p->held[n]);
}

void pn_all_off(pn_piano_t* p) {
  p->pedal = 0;
  for (int n = 0; n < PN_NOTES; n++) {
    p->held[n] = 0;
    damp(p, n, 1);
  }
}

void pn_render(pn_piano_t* p, float* out) {
  for (int s = 0; s < PN_BLOCK; s++) out[s] = 0;
  // Two oscillators at a time, so one's multiplies fill the other's wait. Each sample still adds them in order.
  int i = 0;
  for (; i + 1 < p->count; i += 2) {
    float ar = p->zr[i], ai = p->zi[i], awr = p->wr[i], awi = p->wi[i];
    float br = p->zr[i + 1], bi = p->zi[i + 1], bwr = p->wr[i + 1], bwi = p->wi[i + 1];
    for (int s = 0; s < PN_BLOCK; s++) {
      float ta = ar * awr - ai * awi, tb = br * bwr - bi * bwi;
      ai = ar * awi + ai * awr;
      bi = br * bwi + bi * bwr;
      ar = ta;
      br = tb;
      out[s] += ai;
      out[s] += bi;
    }
    p->zr[i] = ar;
    p->zi[i] = ai;
    p->mag2[i] = ar * ar + ai * ai;
    p->zr[i + 1] = br;
    p->zi[i + 1] = bi;
    p->mag2[i + 1] = br * br + bi * bi;
  }
  for (; i < p->count; i++) {
    float zr = p->zr[i], zi = p->zi[i], wr = p->wr[i], wi = p->wi[i];
    for (int s = 0; s < PN_BLOCK; s++) {
      float t = zr * wr - zi * wi;
      zi = zr * wi + zi * wr;
      zr = t;
      out[s] += zi;
    }
    p->zr[i] = zr;
    p->zi[i] = zi;
    p->mag2[i] = zr * zr + zi * zi;
  }
  for (int n = 0; n < PN_NOTES; n++) p->level[n] = 0;
  for (int i = 0; i < p->count; i++) p->level[p->note[i]] += p->mag2[i];
  for (int i = 0; i < p->count;) {
    if (p->mag2[i] < p->cull2) {
      int l = --p->count;
      p->zr[i] = p->zr[l];
      p->zi[i] = p->zi[l];
      p->wr[i] = p->wr[l];
      p->wi[i] = p->wi[l];
      p->r0[i] = p->r0[l];
      p->rd[i] = p->rd[l];
      p->mag2[i] = p->mag2[l];
      p->note[i] = p->note[l];
    } else {
      i++;
    }
  }
  // The board: each strike's burst of noise, shaped as a hump over the hammer's contact, drives the modes.
  if (p->board_live > 0) {
    p->board_live -= PN_BLOCK;
    for (int s = 0; s < PN_BLOCK; s++) {
      float x[PN_BOARD];
      for (int m = 0; m < PN_BOARD; m++) x[m] = 0;
      for (int b = 0; b < PN_STRIKES; b++) {
        int t = p->strike[b].t, len = p->strike[b].len;
        if (t >= len) continue;
        float u = (float)(2 * t < len ? 2 * t : 2 * (len - t)) / (float)len;
        float e = (float)(int32_t)rnd(p) * (1.0f / 2147483648.0f) * u;
        for (int m = 0; m < PN_BOARD; m++) x[m] += p->strike[b].g[m] * e;
        p->strike[b].t = t + 1;
      }
      float y = 0;
      for (int m = 0; m < PN_BOARD; m++) {
        float v = p->board_b[m] * x[m] + p->board_c1[m] * p->board_y1[m] - p->board_c2[m] * p->board_y2[m];
        p->board_y2[m] = p->board_y1[m];
        p->board_y1[m] = v;
        y += v;
      }
      out[s] += y;
    }
  }
}
