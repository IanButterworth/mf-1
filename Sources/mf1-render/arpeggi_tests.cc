// Checks and recordings for Arpeggi: the pool, the shapes, rhythms and relations, the player and the scenes'
// sounds, through the offline engine.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "ArpeggiCore.h"
#include "MF1Core.h"

namespace {

const double kRate = 48000;
int failures = 0;

void check(bool ok, const std::string& what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) failures++;
}

std::vector<float> render(double seconds) {
  std::vector<float> out((size_t)(kRate * seconds));
  for (size_t i = 0; i < out.size(); i += 64) mf1_offline_render(out.data() + i, (int)std::min<size_t>(64, out.size() - i));
  return out;
}

double db(double x) { return 20 * std::log10(std::max(1e-9, x)); }

double rms(const std::vector<float>& a, double from, double to) {
  size_t i0 = (size_t)(std::max(0.0, from) * kRate), i1 = std::min(a.size(), (size_t)(to * kRate));
  double sum = 0;
  for (size_t i = i0; i < i1; i++) sum += (double)a[i] * a[i];
  return std::sqrt(sum / std::max<size_t>(1, i1 - std::min(i0, i1)));
}

bool finite(const std::vector<float>& a) {
  for (float x : a)
    if (!std::isfinite(x)) return false;
  return true;
}

void silence() {
  mf1_all_notes_off();
  for (int i = 0; i < 300; i++)
    if (i >= 3 && rms(render(0.1), 0, 0.1) < 1e-6) break;
}

void install() {
  arpeggi_install();
  render(0.05);
}

std::string title(int i) {
  ApScene c;
  arpeggi_scene(i, &c);
  return c.title;
}

void writeWav(const char* path, const std::vector<float>& a) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  uint32_t dataBytes = (uint32_t)a.size() * 2, rate = (uint32_t)kRate;
  auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f);
  w32(36 + dataBytes);
  fwrite("WAVEfmt ", 1, 8, f);
  w32(16), w16(1), w16(1), w32(rate), w32(rate * 2), w16(2), w16(16);
  fwrite("data", 1, 4, f);
  w32(dataBytes);
  for (float x : a) w16((uint16_t)(int16_t)std::lround(std::max(-1.f, std::min(1.f, x)) * 32767));
  fclose(f);
}

// A pool latched with these notes.
ApPool pool(std::vector<int> notes, int hold = AP_LATCH, int ring = 4) {
  ApPool p;
  ap_pool_init(&p, hold, ring);
  std::vector<int8_t> n(notes.begin(), notes.end());
  ap_pool_seed(&p, n.data(), (int)n.size());
  return p;
}

std::vector<int> poolNotes(const ApPool& p) { return std::vector<int>(p.note, p.note + p.count); }

// Settings with lane A alone, one octave, every step, unless changed.
ApProc solo(int shape, int rate = AP_R16, int rhythm = AP_EVERY) {
  int32_t s[AP_FIELDS];
  arpeggi_default_scene(0, s);
  s[AP_DUO] = 0, s[AP_A_SHAPE] = shape, s[AP_A_RATE] = rate, s[AP_A_RHYTHM] = rhythm, s[AP_SPAN] = 1;
  s[AP_SWING] = 0, s[AP_A_GATE] = 50;
  ApProc p;
  ap_proc_make(s, &p);
  return p;
}

// Every note the lanes start in [from, to), in order.
std::vector<ApNote> notes(const ApProc& p, const ApPool& pool, int64_t from, int64_t to) {
  std::vector<ApNote> all;
  ApNote n[AP_MAX_OUT];
  for (int64_t t = from; t < to;) {
    int k = ap_next_notes(&p, &pool, t, to, n);
    if (!k) break;
    all.insert(all.end(), n, n + k);
    t = n[0].tick + 1;
  }
  return all;
}

std::vector<int> pitches(const std::vector<ApNote>& ns, int lane = -1) {
  std::vector<int> out;
  for (auto& n : ns)
    if (lane < 0 || n.lane == lane) out.push_back(n.note);
  return out;
}

std::vector<int64_t> ticks(const std::vector<ApNote>& ns, int lane = -1) {
  std::vector<int64_t> out;
  for (auto& n : ns)
    if (lane < 0 || n.lane == lane) out.push_back(n.tick);
  return out;
}

const int64_t S16 = AP_PPQ / 4;
const int64_t BAR = 4 * AP_PPQ;

std::vector<int> first(const ApProc& p, const ApPool& pl, int n) {
  auto v = pitches(notes(p, pl, 0, 64 * BAR));
  if ((int)v.size() > n) v.resize(n);
  return v;
}

void poolTests() {
  // The ring: five slots fill in order, then each key replaces the oldest in its slot.
  ApPool r;
  ap_pool_init(&r, AP_RING, 3);
  check(ap_pool_key_down(&r, 60), "a key into an empty pool starts the clock");
  ap_pool_key_up(&r, 60);
  check(!ap_pool_key_down(&r, 62), "a second key does not start it again");
  ap_pool_key_up(&r, 62);
  ap_pool_key_down(&r, 64);
  ap_pool_key_up(&r, 64);
  check(poolNotes(r) == std::vector<int>({60, 62, 64}), "Ring: keys fill the ring and stay after letting go");
  ap_pool_key_down(&r, 67);
  ap_pool_key_up(&r, 67);
  check(poolNotes(r) == std::vector<int>({67, 62, 64}), "Ring: a new key replaces the oldest, in its slot");
  ap_pool_key_down(&r, 69);
  ap_pool_key_down(&r, 71);
  check(poolNotes(r) == std::vector<int>({67, 69, 71}), "Ring: and the next oldest, round the ring");
  ap_pool_key_down(&r, 71);
  check(poolNotes(r) == std::vector<int>({71, 69, 71}), "Ring: the same note may come twice");
  ap_pool_mode(&r, AP_RING, 2);
  check(poolNotes(r) == std::vector<int>({71, 71}), "Ring: a smaller ring keeps its newest notes");
  ap_pool_mode(&r, AP_RING, 4);
  ap_pool_key_down(&r, 72);
  check(poolNotes(r) == std::vector<int>({71, 71, 72}), "Ring: a larger ring fills its new slots first");

  ApPool h;
  ap_pool_init(&h, AP_HOLD, 4);
  ap_pool_key_down(&h, 60);
  ap_pool_key_down(&h, 64);
  ap_pool_key_up(&h, 60);
  check(poolNotes(h) == std::vector<int>({64}), "Hold: a key letting go leaves the pool");
  ap_pool_key_up(&h, 64);
  check(h.count == 0, "Hold: with every key up the pool is empty");

  ApPool l;
  ap_pool_init(&l, AP_LATCH, 4);
  ap_pool_key_down(&l, 60);
  ap_pool_key_down(&l, 64);
  ap_pool_key_up(&l, 60);
  ap_pool_key_down(&l, 67);
  ap_pool_key_up(&l, 64);
  ap_pool_key_up(&l, 67);
  check(poolNotes(l) == std::vector<int>({60, 64, 67}), "Latch: keys added while one is held join the chord");
  ap_pool_key_down(&l, 62);
  check(poolNotes(l) == std::vector<int>({62}), "Latch: a key after all let go starts a new chord");
  ap_pool_key_up(&l, 62);
  ApPool seeded = pool({60, 64, 67}, AP_HOLD);
  ap_pool_key_down(&seeded, 72);
  check(poolNotes(seeded) == std::vector<int>({72}), "Hold: the first key takes over from the seed");
}

void shapeTests() {
  const ApPool c = pool({64, 60, 67});
  check(first(solo(AP_UP), c, 6) == std::vector<int>({60, 64, 67, 60, 64, 67}), "Up");
  check(first(solo(AP_DOWN), c, 4) == std::vector<int>({67, 64, 60, 67}), "Down");
  check(first(solo(AP_BOUNCE), c, 6) == std::vector<int>({60, 64, 67, 64, 60, 64}), "Bounce does not repeat its ends");
  check(first(solo(AP_ORDER), c, 4) == std::vector<int>({64, 60, 67, 64}), "Order plays the notes as they came");
  const ApPool five = pool({60, 62, 64, 65, 67});
  check(first(solo(AP_CONVERGE), five, 5) == std::vector<int>({60, 67, 62, 65, 64}), "Converge closes in");
  check(first(solo(AP_THUMB), c, 6) == std::vector<int>({60, 64, 60, 67, 60, 64}), "Thumb comes back to the lowest");
  check(first(solo(AP_SPIRAL), c, 6) == std::vector<int>({60, 64, 67, 64, 67, 60}),
        "Spiral starts each pass a note higher");
  ApProc two = solo(AP_UP);
  two.span = 2;
  check(first(two, c, 6) == std::vector<int>({60, 64, 67, 72, 76, 79}), "Span climbs through the octaves");

  const ApPool eight = pool({60, 62, 64, 65, 67, 69, 71, 72});
  auto sh = first(solo(AP_SHUFFLE), eight, 32);
  bool each = true;
  for (int pass = 0; pass < 4; pass++) {
    std::set<int> seen(sh.begin() + pass * 8, sh.begin() + pass * 8 + 8);
    each = each && seen.size() == 8;
  }
  check(each, "Shuffle plays every note once a pass");
  check(!std::equal(sh.begin(), sh.begin() + 8, sh.begin() + 8), "Shuffle changes the order each pass");
  check(first(solo(AP_SHUFFLE), eight, 32) == sh, "Shuffle is the same every time it is played");
  auto w = first(solo(AP_WALK), eight, 16);
  bool steps = true;
  std::vector<int> sorted = {60, 62, 64, 65, 67, 69, 71, 72};
  for (size_t i = 1; i < w.size(); i++) {
    int a = (int)(std::find(sorted.begin(), sorted.end(), w[i - 1]) - sorted.begin());
    int b = (int)(std::find(sorted.begin(), sorted.end(), w[i]) - sorted.begin());
    steps = steps && std::abs(a - b) == 1;
  }
  check(steps, "Walk moves a note up or down at a time");

  auto ch = notes(solo(AP_CHORD), c, 0, 2 * S16);
  check(ch.size() == 6 && ch[0].tick == 0 && ch[2].tick == 0 && ch[3].tick == S16, "Chord strikes all at once");
  ApProc st = solo(AP_STRUM, AP_R4);
  auto sn = notes(st, c, 0, 2 * AP_PPQ);
  check(sn.size() == 6 && sn[1].tick - sn[0].tick == AP_PPQ / 48 && sn[0].note == 60 && sn[3].note == 67,
        "Strum spreads the chord, down and up in turn");
}

void rhythmTests() {
  const ApPool c = pool({60, 64, 67});
  auto t = ticks(notes(solo(AP_UP, AP_R16, AP_E3), c, 0, 8 * S16));
  check(t == std::vector<int64_t>({0, 3 * S16, 6 * S16}), "3 of 8 is the tresillo");
  check(first(solo(AP_UP, AP_R16, AP_E3), c, 3) == std::vector<int>({60, 64, 67}),
        "A rest does not skip a note of the shape");
  auto r = notes(solo(AP_UP, AP_R16, AP_RATCHET), c, 0, 4 * S16);
  check(r.size() == 6 && r[3].note == r[4].note && r[4].tick - r[3].tick == S16 / 3,
        "Ratchet strikes every fourth step three times");
  auto g = ticks(notes(solo(AP_UP, AP_R16, AP_GALLOP), c, 0, 2 * S16));
  check(g == std::vector<int64_t>({0, S16, S16 + S16 / 2}), "Gallop: da, da-da");
  auto f = ticks(notes(solo(AP_UP, AP_R16, AP_FALL), c, 0, 16 * S16));
  bool closer = f.size() == 12;
  for (size_t i = 2; i < f.size(); i++) closer = closer && f[i] - f[i - 1] <= f[i - 1] - f[i - 2];
  check(closer, "Fall: twelve strikes, closer and closer");
  auto sk1 = ticks(notes(solo(AP_UP, AP_R16, AP_SKIP), c, 0, 16 * S16));
  auto sk2 = ticks(notes(solo(AP_UP, AP_R16, AP_SKIP), c, 16 * S16, 32 * S16));
  bool same = sk1.size() == sk2.size() && sk1.size() >= 8 && sk1.size() <= 14;
  for (size_t i = 0; same && i < sk1.size(); i++) same = sk2[i] - sk1[i] == 16 * S16;
  check(same, "Skip leaves out the same steps every time round");

  ApProc sw = solo(AP_UP, AP_R8);
  sw.swing = 100;
  auto s = ticks(notes(sw, c, 0, AP_PPQ));
  check(s == std::vector<int64_t>({0, AP_PPQ / 2 + AP_PPQ / 6}), "Swing makes every second step late");

  // Rates: quintuplets, septuplets and dotted steps are whole numbers of ticks.
  for (int rate = 0; rate < AP_RATE_COUNT; rate++) {
    int k = ap_rate_ticks(rate);
    if (k <= 0) check(false, "rate has ticks");
  }
  check(ap_rate_ticks(AP_R16Q) * 5 == AP_PPQ && ap_rate_ticks(AP_R16S) * 7 == AP_PPQ, "five and seven to a beat");

  // A lane's time comes from the tick alone: far into a run it is where the arithmetic says.
  const int64_t far = (int64_t)AP_PPQ * 1000000;
  auto late = notes(solo(AP_UP, AP_R16S), c, far, far + AP_PPQ);
  check(late.size() == 7 && late[0].tick == far && late[0].note == 64,
        "a million beats in, septuplets still fall on the beat");
}

ApProc duo(int relation, int shapeA, int shapeB, int rateA = AP_R16, int rateB = AP_R16, int pitch = 0) {
  ApProc p = solo(shapeA, rateA);
  p.duo = 1, p.relation = relation;
  p.shape[1] = shapeB, p.rate[1] = rateB, p.rhythm[1] = AP_EVERY, p.gate[1] = 50, p.pitch = pitch;
  return p;
}

void relationTests() {
  const ApPool c = pool({60, 64, 67});
  auto tog = notes(duo(AP_TOGETHER, AP_UP, AP_DOWN, AP_R16, AP_R16T, 12), c, 0, AP_PPQ);
  check(ticks(tog, 0).size() == 4 && ticks(tog, 1).size() == 6 && pitches(tog, 1)[0] == 79,
        "Together: each lane its own rate, B at its pitch");
  auto hk = notes(duo(AP_HOCKET, AP_UP, AP_DOWN), c, 0, 6 * S16);
  check(pitches(hk) == std::vector<int>({60, 67, 64, 64, 67, 60}) && hk[1].lane == 1 && hk[2].lane == 0,
        "Hocket: one line, A's shape and B's in turn");
  auto an = notes(duo(AP_ANSWER, AP_UP, AP_DOWN), c, 0, 2 * BAR);
  bool split = !an.empty();
  for (auto& n : an) split = split && n.lane == (n.tick >= BAR ? 1 : 0);
  check(split, "Answer: A plays the first bar, B the second");
  ApProc cp = duo(AP_CANON, AP_SHUFFLE, AP_UP, AP_R8, AP_R16, 12);
  auto cn = notes(cp, pool({60, 62, 64, 65, 67}), 0, 4 * BAR);
  auto a = notes(cp, pool({60, 62, 64, 65, 67}), 0, 4 * BAR);
  std::vector<std::pair<int64_t, int>> la, lb;
  for (auto& n : cn) (n.lane ? lb : la).push_back({n.tick, n.note});
  bool follows = lb.size() >= 8;
  for (size_t i = 0; follows && i < 8; i++)
    follows = lb[i].first == la[i].first + 3 * AP_PPQ / 2 && lb[i].second == la[i].second + 12;
  check(follows, "Canon: B plays A's line three steps behind, at its pitch");
  (void)a;
  auto mi = notes(duo(AP_MIRROR, AP_UP, AP_UP), c, 0, 3 * S16);
  check(pitches(mi, 0) == std::vector<int>({60, 64, 67}) && pitches(mi, 1) == std::vector<int>({67, 64, 60}),
        "Mirror: B plays A's shape upside down");
  ApProc tr = duo(AP_TRADE, AP_UP, AP_DOWN);
  auto later = [&](const ApProc& p) { return pitches(notes(p, c, 2 * BAR, 3 * BAR), 0); };
  check(pitches(notes(tr, c, 0, BAR), 0) == pitches(notes(solo(AP_UP), c, 0, BAR)) &&
            later(tr) == later(solo(AP_DOWN)),
        "Trade: the lanes swap shapes after two bars");
}

// Plays a scene through the offline engine for `seconds`, in 64 sample blocks with the player's clock in step.
std::vector<float> perform(int scene, double seconds) {
  int32_t s[AP_FIELDS];
  arpeggi_default_scene(scene, s);
  mf1_global_set(MF1_G_QUIRKS, 1);
  arpeggi_load(s);
  silence();
  arpeggi_offline(true);
  ApProc p;
  ap_proc_make(s, &p);
  arpeggi_set(&p);
  arpeggi_pool_mode(s[AP_HOLD_MODE], s[AP_RING_SIZE]);
  int8_t seed[AP_MAX_POOL];
  int n = arpeggi_default_seed(scene, seed);
  arpeggi_seed(seed, n);
  std::vector<float> out((size_t)(kRate * seconds));
  for (size_t i = 0; i < out.size(); i += 64) {
    arpeggi_offline_seek(i / kRate);
    mf1_offline_render(out.data() + i, (int)std::min<size_t>(64, out.size() - i));
  }
  return out;
}

int held() {
  int n = 0;
  for (int k = 0; k < 128; k++) n += arpeggi_refs(k);
  return n;
}

void playerTests() {
  install();
  for (int i = 0; i < arpeggi_scene_count(); i++) {
    auto a = perform(i, 4);
    double top = 0;
    for (double t = 0; t + 0.05 <= 4; t += 0.05) top = std::max(top, rms(a, t, t + 0.05));
    check(finite(a) && db(top) > -40 && db(top) < 0,
          title(i) + " plays, loudest 50 ms " + std::to_string((int)std::lround(db(top))) + " dB");
    arpeggi_stop();
    check(held() == 0, title(i) + ": stop lets go of every note");
  }
  // A key that finds the clock stopped sounds at once.
  arpeggi_clear();
  arpeggi_offline_seek(10);
  arpeggi_pool_mode(AP_HOLD, 4);
  arpeggi_key_down(60);
  check(arpeggi_refs(60) == 1, "the first key's note starts at once");
  arpeggi_play_down(60);
  arpeggi_key_up(60);
  arpeggi_offline_seek(11);
  check(arpeggi_refs(60) == 1 && !arpeggi_playing(), "Hold: letting go stops the clock, a key played along stays");
  arpeggi_play_up(60);
  check(held() == 0, "and lets go when its key does");
  arpeggi_offline(false);
  silence();
}

void voiceTests() {
  uint8_t bank[4104];
  arpeggi_export_bank(bank);
  int sum = 0;
  for (int i = 0; i < 4096; i++) sum += bank[6 + i];
  check(bank[0] == 0xF0 && bank[4103] == 0xF7 && ((sum + bank[4102]) & 0x7f) == 0, "the bank dump is valid");
  bool ok = true;
  for (int i = 0; i < arpeggi_scene_count(); i++) {
    int32_t s[AP_FIELDS];
    arpeggi_default_scene(i, s);
    int32_t c[AP_FIELDS];
    memcpy(c, s, sizeof(c));
    arpeggi_clamp(c);
    ok = ok && memcmp(c, s, sizeof(c)) == 0;
    uint8_t v[155];
    arpeggi_voice(s, v);
    for (int k = 0; k < 155; k++) ok = ok && v[k] < 128;
  }
  check(ok, "every scene is in range and its voice is valid VCED");
}

}  // namespace

void arpeggiReport(int note) {
  install();
  printf("note %d, dry, held 0.3 s | loudest dB | within 20 dB, 40 dB (ms)\n", note);
  for (int snd = 0; snd < AP_SOUND_COUNT; snd++) {
    int32_t s[AP_FIELDS];
    arpeggi_default_scene(0, s);
    s[AP_SOUND] = snd, s[AP_ROOM] = 0, s[AP_ECHO] = 0;
    arpeggi_load(s);
    silence();
    render(0.05);
    mf1_key_down(note);
    auto a = render(0.3);
    mf1_key_up(note);
    auto b = render(2.0);
    a.insert(a.end(), b.begin(), b.end());
    double top = 0;
    for (double t = 0; t + 0.01 <= a.size() / kRate; t += 0.005) top = std::max(top, rms(a, t, t + 0.01));
    double l20 = 0, l40 = 0;
    for (double t = 0; t + 0.01 <= a.size() / kRate; t += 0.005) {
      double r = rms(a, t, t + 0.01);
      if (r > top * 0.1) l20 = t + 0.01;
      if (r > top * 0.01) l40 = t + 0.01;
    }
    printf("%-7s %6.1f | %5.0f %5.0f\n", arpeggi_sound_name(snd), db(top), l20 * 1000, l40 * 1000);
  }
}

void arpeggiWav(int scene, const char* path, double seconds) {
  install();
  auto a = perform(scene, seconds);
  arpeggi_stop();
  arpeggi_offline(false);
  writeWav(path, a);
  double top = 0;
  for (double t = 0; t + 0.05 <= seconds; t += 0.05) top = std::max(top, rms(a, t, t + 0.05));
  printf("wrote %s: %s, loudest 50 ms %.1f dB\n", path, title(scene).c_str(), db(top));
}

int arpeggiTests() {
  failures = 0;
  printf("-- arpeggi\n");
  poolTests();
  shapeTests();
  rhythmTests();
  relationTests();
  voiceTests();
  playerTests();
  return failures;
}
