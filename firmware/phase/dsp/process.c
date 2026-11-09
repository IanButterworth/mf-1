// The processes, in plain C with integer arithmetic, so a firmware can run them as they are.
//
// Time is counted in ticks from the start of the process, PH_TPS to a step of part 1. Each part's position is a
// function of that count alone, so no rounding builds up: after any number of repeats, part 2 is exactly where
// the process says it should be. A part starts a note whenever its position passes into a new step. It uses no C
// library, so the firmware builds this same file.
#include "PhaseCore.h"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Division and remainder rounded down, for times before the start.
static int64_t fdiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  return (a % b != 0 && a < 0) ? q - 1 : q;
}

static int64_t fmod64(int64_t a, int64_t b) {
  int64_t r = a % b;
  return r < 0 ? r + b : r;
}

// Every 64-bit product is made here, out of line: on the FM-1's CPU a 64-bit multiply next to a memory load can
// corrupt a register (felucca-bsp's docs/CPU.md).
__attribute__((noinline)) static int64_t mul(int64_t a, int64_t b) { return a * b; }

// Semitones each part sits from the cell, for each Spread.
static const int kSpread[PH_SPREAD_COUNT][PH_MAX_PARTS] = {
    {0, 0, 0, 0}, {0, 0, 12, 12}, {0, 12, -12, 12}, {0, 12, -12, 24}};

// Repeats it takes a drifting part to gain one step: from 16 at Motion 0 to 1 at 100.
static int drift_repeats(int motion) { return 1 + (100 - motion) * 15 / 100; }
// Steps a Shift jumps: 1 up to 4.
static int jump_steps(int motion) { return 1 + motion * 3 / 100; }
// Steps a following part sits behind, for Build and Canon.
static int gap_steps(const PhaseProc* p, int least) {
  return clampi((p->motion * (p->length - 1) + 50) / 100, least, p->length - 1);
}

// Up from 0 to n and back down, one step per stage.
static int tri(int64_t j, int n) {
  if (n <= 0) return 0;
  int64_t m = fmod64(j, 2 * (int64_t)n);
  return (int)(m <= n ? m : 2 * n - m);
}

static int64_t cycle_ticks(const PhaseProc* p) { return mul(p->length, PH_TPS); }
static int64_t stage_ticks(const PhaseProc* p) { return mul(p->repeats, cycle_ticks(p)); }

static int note_count(const PhaseProc* p) {
  int n = 0;
  for (int i = 0; i < p->length; i++) n += p->note[i] >= 0;
  return n;
}

// The k-th note of the cell, counting round.
static int nth_note(const PhaseProc* p, int k) {
  for (int i = 0, seen = 0; i < p->length; i++)
    if (p->note[i] >= 0 && seen++ == k) return p->note[i];
  return -1;
}

void phase_proc_make(const int32_t s[PH_FIELDS], const PhaseCell* cell, PhaseProc* p) {
  *p = (PhaseProc){0};
  p->process = clampi(s[PH_PROCESS], 0, PH_PROCESS_COUNT - 1);
  p->parts = clampi(s[PH_PARTS], 1, PH_MAX_PARTS);
  p->length = clampi(s[PH_LENGTH], 2, PH_MAX_STEPS);
  p->repeats = clampi(s[PH_REPEATS], 1, 16);
  p->motion = clampi(s[PH_MOTION], 0, 100);
  p->gate = clampi(s[PH_GATE], 5, 100);
  p->transpose = clampi(s[PH_TRANSPOSE], -12, 12);
  p->spread = clampi(s[PH_SPREAD], 0, PH_SPREAD_COUNT - 1);
  p->tempo = clampi(s[PH_TEMPO], 40, 200);
  const int written = cell ? clampi(cell->length, 0, PH_MAX_STEPS) : 0;
  int count = 0;
  for (int i = 0; i < PH_MAX_STEPS; i++) {
    int n = i < p->length && i < written ? cell->note[i] : -1;
    p->note[i] = (int8_t)(n < 0 || n > 127 ? -1 : n);
    count += p->note[i] >= 0;
  }

  // Density drops notes in a fixed scattered order, keeping the first step to the last.
  int keep = (count * clampi(s[PH_DENSITY], 0, 100) + 50) / 100;
  if (count > 0 && keep < 1) keep = 1;
  while (count > keep) {
    int drop = -1, worst = -1;
    for (int i = 1; i < p->length; i++)
      if (p->note[i] >= 0 && (i * 7) % 17 > worst) worst = (i * 7) % 17, drop = i;
    if (drop < 0) break;
    p->note[drop] = -1;
    count--;
  }

  // Build fills each rest with the note before it, in another scattered order.
  for (int i = 0; i < PH_MAX_STEPS; i++) p->fill[i] = -1, p->order[i] = -1;
  if (count == 0) return;
  for (int i = 0; i < p->length; i++) {
    if (p->note[i] >= 0) continue;
    for (int back = 1; back <= p->length; back++) {
      int n = p->note[(i - back + p->length) % p->length];
      if (n >= 0) {
        p->fill[i] = (int8_t)n;
        break;
      }
    }
    int rank = 0;
    for (int k = 0; k < p->length; k++)
      if (p->note[k] < 0 && (k * 5) % 17 < (i * 5) % 17) rank++;
    p->order[i] = (int8_t)rank;
    p->rests++;
  }
}

int phase_motion_steps(int process, int motion, int length) {
  PhaseProc p = {0};
  p.motion = clampi(motion, 0, 100), p.length = clampi(length, 2, PH_MAX_STEPS);
  switch (process) {
    case PH_PHASE:
    case PH_RESULT:
    case PH_TAPE: return drift_repeats(p.motion);
    case PH_SHIFT: return jump_steps(p.motion);
    case PH_BUILD: return gap_steps(&p, 0);
    case PH_CANON: return gap_steps(&p, 1);
    default: return p.motion;
  }
}

bool phase_proc_same_motion(const PhaseProc* a, const PhaseProc* b) {
  return a->process == b->process && a->parts == b->parts && a->length == b->length && a->repeats == b->repeats &&
         a->motion == b->motion;
}

// For Phase and Result: how far part 2 is ahead of part 1, in ticks. Each stage holds part 2 locked for Repeats
// repeats, then lets it drift a whole step ahead over the drift time, running faster by one step in that many.
static int64_t phase_lead(const PhaseProc* p, int64_t u, bool* drifting, int64_t* stage) {
  const int64_t c = cycle_ticks(p), h = p->repeats, d = drift_repeats(p->motion);
  const int64_t len = mul(h + d, c);
  const int64_t s = fdiv(u, len), w = u - mul(s, len);
  const bool dr = w >= mul(h, c);
  if (drifting) *drifting = dr;
  if (stage) *stage = s;
  return mul(s, PH_TPS) + (dr ? (w - mul(h, c)) / mul(d, p->length) : 0);
}

// Part q's position in its own count, in ticks.
static int64_t part_pos(const PhaseProc* p, int q, int64_t u) {
  switch (p->process) {
    case PH_PHASE: return u + mul(q, phase_lead(p, u, 0, 0));
    case PH_RESULT: return q == 1 ? u + phase_lead(p, u, 0, 0) : u;
    case PH_SHIFT: return u + mul(mul(q, fdiv(u, stage_ticks(p))), jump_steps(p->motion) * PH_TPS);
    case PH_BUILD: return u - mul(q, gap_steps(p, 0) * PH_TPS);
    case PH_CANON: return u - mul(q, gap_steps(p, 1) * PH_TPS);
    case PH_STRETCH: return fdiv(u, q + 1);
    case PH_TAPE: return u + mul(q, fdiv(u, mul(drift_repeats(p->motion), p->length)));
    default: return u;
  }
}

// How full a Pulse wave is, 0 .. 1000: least at the edges of a stage, where the chord changes, and most halfway.
static int pulse_level(const PhaseProc* p, int64_t u) {
  const int64_t sl = stage_ticks(p), w = fmod64(u, sl);
  int64_t t = mul(2000, w) / sl - 1000;
  t = 1000 - (t < 0 ? -t : t);
  return (int)(1000 - mul(p->motion, 1000 - t) / 100);
}

static bool part_active(const PhaseProc* p, int q, int64_t u, int64_t pos) {
  if (q >= p->parts) return false;
  switch (p->process) {
    case PH_BUILD: return pos >= 0;
    case PH_CANON: {
      if (pos < 0) return false;
      const int n = p->parts;
      if (n == 1) return true;
      // Entering one a stage, all together, then leaving last in first out.
      const int64_t j = fmod64(fdiv(u, stage_ticks(p)), 3 * n);
      const int64_t on = j < n ? j + 1 : j < 2 * n ? n : 3 * n - j;
      return q < on;
    }
    case PH_RESULT: {
      if (q < 2) return true;
      bool dr;
      phase_lead(p, u, &dr, 0);
      return !dr;
    }
    case PH_PULSE: {
      int on = (p->parts * pulse_level(p, u) + 999) / 1000;
      return q < clampi(on, 1, p->parts);
    }
    default: return true;
  }
}

// What part q plays at step idx of its count, at tick u, and for how long. -1 for nothing.
static int note_at(const PhaseProc* p, int q, int64_t u, int64_t idx, int32_t* len) {
  const int i = (int)fmod64(idx, p->length);
  int gate = p->gate * PH_TPS / 100;
  if (gate < PH_TPS / 16) gate = PH_TPS / 16;
  int n = p->note[i];
  int64_t l = gate;
  switch (p->process) {
    case PH_BUILD:
      if (n < 0 && p->order[i] >= 0 && p->order[i] < tri(fdiv(u, stage_ticks(p)), p->rests)) n = p->fill[i];
      break;
    case PH_PULSE:
      if (n >= 0) {
        // The chord moves one note along the cell each stage; part q plays the q-th note of it.
        const int count = note_count(p);
        n = nth_note(p, (int)fmod64(fdiv(u, stage_ticks(p)) + q, count));
        l = mul(gate, 350 + 650 * pulse_level(p, u) / 1000) / 1000;
      }
      break;
    case PH_STRETCH: {
      // Each stage adds to every note, up over eight stages and back down. Later parts play slower, and their
      // notes are longer by as much.
      int64_t own = gate + mul(tri(fdiv(u, stage_ticks(p)), 8), p->motion * PH_TPS) / 50;
      if (own > cycle_ticks(p)) own = cycle_ticks(p);
      l = mul(own, q + 1);
      break;
    }
    case PH_RESULT:
      if (q >= 2) {
        // The third part plays the upper of the two notes an octave up where it is high, the fourth the lower an
        // octave down where it is low, but only where the two parts differ: in unison there is no new line.
        int64_t s;
        phase_lead(p, u, 0, &s);
        const int a = p->note[i], b = p->note[(int)fmod64(i + s, p->length)];
        n = -1;
        if (a >= 0 && b >= 0 && a != b) {
          int lo = 127, hi = 0;
          for (int k = 0; k < p->length; k++)
            if (p->note[k] >= 0) lo = p->note[k] < lo ? p->note[k] : lo, hi = p->note[k] > hi ? p->note[k] : hi;
          const int mid = (lo + hi) / 2, top = a > b ? a : b, bottom = a < b ? a : b;
          if (q == 2 && top >= mid) n = top + 12;
          if (q == 3 && bottom <= mid) n = bottom - 12;
        }
      }
      break;
    default: break;
  }
  if (n < 0) return -1;
  n += p->transpose + kSpread[p->spread][q];
  if (n < 0 || n > 127) return -1;
  *len = (int32_t)l;
  return n;
}

// The first note part q starts in [from, to). Positions only ever move forward, so the tick a part passes into a
// new step is found by halving.
static bool part_next(const PhaseProc* p, int q, int64_t from, int64_t to, PhaseNote* out) {
  int64_t u = from;
  while (u < to) {
    const int64_t prev = fdiv(part_pos(p, q, u - 1), PH_TPS);
    if (fdiv(part_pos(p, q, to - 1), PH_TPS) <= prev) return false;
    int64_t lo = u, hi = to - 1;
    while (lo < hi) {
      int64_t mid = lo + (hi - lo) / 2;
      if (fdiv(part_pos(p, q, mid), PH_TPS) > prev)
        hi = mid;
      else
        lo = mid + 1;
    }
    const int64_t pos = part_pos(p, q, lo), idx = fdiv(pos, PH_TPS);
    int32_t len = 0;
    int n;
    if (part_active(p, q, lo, pos) && (n = note_at(p, q, lo, idx, &len)) >= 0) {
      out->tick = lo, out->part = q, out->step = (int)idx, out->note = n, out->length = len;
      return true;
    }
    u = lo + 1;
  }
  return false;
}

int phase_next_notes(const PhaseProc* p, int64_t from, int64_t to, PhaseNote out[PH_MAX_PARTS]) {
  int count = 0;
  for (int q = 0; q < p->parts; q++) {
    PhaseNote n;
    if (!part_next(p, q, from, count ? out[0].tick + 1 : to, &n)) continue;
    if (count && n.tick < out[0].tick) count = 0;
    out[count++] = n;
  }
  return count;
}

void phase_view(const PhaseProc* p, int64_t u, PhaseView* v) {
  *v = (PhaseView){0};
  const int64_t c = cycle_ticks(p), sl = stage_ticks(p);
  v->parts = p->parts;
  v->length = p->length;
  if (p->parts >= 2 && p->process != PH_STRETCH && p->process != PH_PULSE) {
    const int64_t lead = part_pos(p, 1, u) - part_pos(p, 0, u);
    v->lead_milli = (int32_t)(mul(lead, 1000) / PH_TPS);
    v->offset_milli = (int32_t)(mul(fmod64(lead, c), 1000) / PH_TPS);
  }
  const int64_t j = fdiv(u, sl);
  switch (p->process) {
    case PH_PHASE:
    case PH_RESULT: {
      int64_t s;
      phase_lead(p, u, &v->drifting, &s);
      v->stage = (int)fmod64(s, p->length), v->stages = p->length;
      break;
    }
    case PH_SHIFT: v->stage = (int)fmod64(j, p->length), v->stages = p->length; break;
    case PH_BUILD: v->stages = 2 * p->rests, v->stage = v->stages ? (int)fmod64(j, v->stages) : 0; break;
    case PH_CANON: v->stages = 3 * p->parts, v->stage = (int)fmod64(j, v->stages); break;
    case PH_PULSE: v->stages = note_count(p), v->stage = v->stages ? (int)fmod64(j, v->stages) : 0; break;
    case PH_STRETCH: v->stages = 16, v->stage = (int)fmod64(j, 16); break;
    case PH_TAPE: v->drifting = true, v->stages = p->length, v->stage = (int)fmod64(v->offset_milli / 1000, p->length); break;
  }
  for (int q = 0; q < PH_MAX_PARTS; q++) {
    const int64_t pos = part_pos(p, q, u);
    const int rate = p->process == PH_STRETCH ? q + 1 : 1;
    v->part[q].active = part_active(p, q, u, pos);
    v->part[q].pos_milli = (int32_t)(mul(fmod64(pos, c), 1000) / PH_TPS);
    for (int i = 0; i < PH_MAX_STEPS; i++) {
      int32_t len = 0;
      const int n = q < p->parts && i < p->length ? note_at(p, q, u, i, &len) : -1;
      v->part[q].note[i] = (int8_t)n;
      v->part[q].len_milli[i] = n >= 0 ? (int32_t)(mul(len, 1000) / (PH_TPS * rate)) : 0;
    }
  }
}
