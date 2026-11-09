// Checks and recordings for Phase: the pieces' sounds, the processes' arithmetic and the player, through the
// offline engine.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "MF1Core.h"
#include "PhaseCore.h"

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

// Zero crossings per second, a rough brightness.
double crossings(const std::vector<float>& a, double from, double to) {
  size_t i0 = (size_t)(from * kRate), i1 = std::min(a.size(), (size_t)(to * kRate));
  int n = 0;
  for (size_t i = i0 + 1; i < i1; i++) n += (a[i - 1] < 0) != (a[i] < 0);
  return n / std::max(1e-9, (double)(i1 - i0) / kRate);
}

void silence() {
  mf1_all_notes_off();
  for (int i = 0; i < 300; i++)
    if (i >= 3 && rms(render(0.1), 0, 0.1) < 1e-6) break;
}

// Offline, the engine drops commands once its queue is full, so render a little after the 128 bank presets.
void install() {
  phase_install();
  render(0.05);
}

std::string title(int i) {
  PhasePiece p;
  phase_piece(i, &p);
  return p.title;
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

// One note of a sound, dry, held for `hold` seconds and then let go for `tail`.
std::vector<float> strike(int sound, int note, double hold, double tail) {
  int32_t s[PH_FIELDS];
  phase_default_piece(0, s);
  s[PH_SOUND] = sound, s[PH_ROOM] = 0, s[PH_ECHO] = 0;
  mf1_global_set(MF1_G_QUIRKS, 1);
  phase_load(s);
  silence();
  render(0.05);
  mf1_key_down(note);
  auto a = render(hold);
  mf1_key_up(note);
  auto b = render(tail);
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// The loudest 10 ms, and how long the level stays within `range` dB of it.
void envelope(const std::vector<float>& a, double range, double* top, double* length) {
  *top = 0;
  for (double t = 0; t + 0.01 <= a.size() / kRate; t += 0.005) *top = std::max(*top, rms(a, t, t + 0.01));
  double last = 0;
  for (double t = 0; t + 0.01 <= a.size() / kRate; t += 0.005)
    if (rms(a, t, t + 0.01) > *top * std::pow(10, -range / 20)) last = t + 0.01;
  *length = last;
}

// Plays a piece's process through the offline engine for `seconds`, in 64 sample blocks with the player's clock in
// step, so every note lands where the engine would put it.
std::vector<float> perform(const int32_t* s, const PhaseCell& cell, double seconds) {
  mf1_global_set(MF1_G_QUIRKS, 1);
  phase_load(s);
  silence();
  phase_offline(true);
  PhaseProc p;
  phase_proc_make(s, &cell, &p);
  phase_start(&p, 0);
  std::vector<float> out((size_t)(kRate * seconds));
  for (size_t i = 0; i < out.size(); i += 64) {
    phase_offline_seek(i / kRate);
    mf1_offline_render(out.data() + i, (int)std::min<size_t>(64, out.size() - i));
  }
  return out;
}

// Every note the process starts in [from, to), in order.
std::vector<PhaseNote> notes(const PhaseProc& p, int64_t from, int64_t to) {
  std::vector<PhaseNote> all;
  PhaseNote n[PH_MAX_PARTS];
  for (int64_t t = from; t < to;) {
    int k = phase_next_notes(&p, t, to, n);
    if (!k) break;
    all.insert(all.end(), n, n + k);
    t = n[0].tick + 1;
  }
  return all;
}

int countPart(const std::vector<PhaseNote>& ns, int part) {
  int n = 0;
  for (auto& x : ns) n += x.part == part;
  return n;
}

PhaseProc proc(int piece, void (*edit)(int32_t*) = nullptr, PhaseCell* cellOut = nullptr) {
  int32_t s[PH_FIELDS];
  phase_default_piece(piece, s);
  if (edit) edit(s);
  PhaseCell c;
  phase_default_cell(piece, &c);
  if (cellOut) *cellOut = c;
  PhaseProc p;
  phase_proc_make(s, &c, &p);
  return p;
}

const int64_t T = PH_TPS;

}  // namespace

void phaseReport(int note) {
  install();
  printf("note %d, dry, held 1.5 s | loudest dB | within 20 dB, 40 dB (ms) | crossings/s at 20 ms, 300 ms\n", note);
  for (int s = 0; s < PH_SOUND_COUNT; s++) {
    auto a = strike(s, note, 1.5, 1.0);
    double top, l20, l40;
    envelope(a, 20, &top, &l20);
    envelope(a, 40, &top, &l40);
    printf("%-8s %6.1f | %5.0f %5.0f | %6.0f %6.0f\n", phase_sound_name(s), db(top), l20 * 1000, l40 * 1000,
           crossings(a, 0.02, 0.06), crossings(a, 0.3, 0.34));
  }
}

// A piece's process for `seconds`, with its own room and echo.
void phaseWav(int piece, const char* path, double seconds) {
  install();
  int32_t s[PH_FIELDS];
  phase_default_piece(piece, s);
  PhaseCell c;
  phase_default_cell(piece, &c);
  auto a = perform(s, c, seconds);
  phase_stop();
  phase_offline(false);
  writeWav(path, a);
  double top = 0;
  for (double t = 0; t + 0.05 <= seconds; t += 0.05) top = std::max(top, rms(a, t, t + 0.05));
  printf("wrote %s: %s, loudest 50 ms %.1f dB\n", path, title(piece).c_str(), db(top));
}

// Runs the real player thread for a few seconds against the offline engine and prints how late its notes left.
void phaseJitter(double seconds) {
  install();
  int32_t s[PH_FIELDS];
  phase_default_piece(0, s);
  PhaseCell c;
  phase_default_cell(0, &c);
  PhaseProc p;
  phase_proc_make(s, &c, &p);
  phase_offline(false);
  phase_start(&p, 0.05);
  // Keep the engine's queue drained, as the audio thread would.
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000));
  std::vector<float> buf(256);
  while (std::chrono::steady_clock::now() < end) {
    mf1_offline_render(buf.data(), 256);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  double t[3];
  phase_timing(t);
  phase_shutdown();
  printf("%.0f notes and offs, late by %.1f us on average, %.1f us at worst\n", t[0], t[1], t[2]);
}

int phaseTests() {
  failures = 0;
  mf1_global_set(MF1_G_QUIRKS, 1);
  install();
  const int n = phase_piece_count();
  check(n == 8, "there are eight pieces");

  {
    bool names = true, ranges = true, voices = true, cells = true;
    int processes[PH_PROCESS_COUNT] = {0}, sounds[PH_SOUND_COUNT] = {0};
    for (int i = 0; i < n; i++) {
      PhasePiece pc;
      names &= phase_piece(i, &pc) && strlen(pc.name) > 0 && strlen(pc.title) > 0 && strlen(pc.blurb) > 0;
      int32_t s[PH_FIELDS], c[PH_FIELDS];
      phase_default_piece(i, s);
      memcpy(c, s, sizeof(s));
      phase_clamp(c);
      ranges &= memcmp(s, c, sizeof(s)) == 0 && s[PH_PIECE] == i;
      processes[s[PH_PROCESS]]++;
      sounds[s[PH_SOUND]]++;
      PhaseCell cell;
      phase_default_cell(i, &cell);
      int notesIn = 0;
      for (int k = 0; k < cell.length; k++) notesIn += cell.note[k] >= 0;
      cells &= cell.length == s[PH_LENGTH] && cell.length >= 8 && cell.length <= PH_MAX_STEPS && notesIn >= 4;
      phase_load(s);
      uint8_t v[155];
      phase_voice(s, v);
      for (int k = 0; k < 145; k++) voices &= mf1_edit_get(k) == v[k];
      for (int k = 0; k < 6; k++) voices &= v[k * 21 + 19] <= 99 && v[k * 21 + 18] <= 31 && v[k * 21 + 16] <= 99;
    }
    check(names, "every piece has a voice name, a title and a few words");
    check(ranges, "every piece's settings are in range and name the piece");
    check(cells, "every cell is 8 to 16 steps with at least four notes, and its length is the piece's");
    bool all = true;
    for (int k = 0; k < PH_PROCESS_COUNT; k++) all &= processes[k] == 1;
    for (int k = 0; k < PH_SOUND_COUNT; k++) all &= sounds[k] == 1;
    check(all, "the pieces show every process and every sound once");
    check(voices, "a loaded piece's edit buffer is exactly its sound, with valid DX7 values");
  }

  {
    uint8_t dump[4104];
    phase_export_bank(dump);
    int sum = 0;
    for (int i = 6; i < 6 + 4096; i++) sum += dump[i];
    check(dump[0] == 0xF0 && dump[3] == 0x09 && dump[4103] == 0xF7 && ((sum + dump[6 + 4096]) & 0x7f) == 0,
          "the sound bank is a valid DX7 32-voice dump");
  }

  // The sounds: all about as loud; the struck ones die away by themselves while held, the wood block within a
  // tenth of a second, the bars and bells ringing longer; the organ and the voice hold, and stop when let go.
  {
    double lo = 1e9, hi = -1e9, len[PH_SOUND_COUNT], tail[PH_SOUND_COUNT], held[PH_SOUND_COUNT];
    bool ok = true;
    for (int s = 0; s < PH_SOUND_COUNT; s++) {
      auto a = strike(s, 60, 1.5, 0.8);
      ok &= finite(a);
      double top;
      envelope(a, 20, &top, &len[s]);
      lo = std::min(lo, db(top)), hi = std::max(hi, db(top));
      held[s] = db(rms(a, 1.3, 1.45)) - db(top);
      tail[s] = db(rms(a, 2.0, 2.3)) - db(top);
    }
    char what[160];
    snprintf(what, sizeof(what), "every sound plays, within 10 dB of the others (%.1f to %.1f dB)", lo, hi);
    check(ok && lo > -40 && hi - lo <= 10, what);
    bool struck = true;
    for (int s : {PH_MARIMBA, PH_VIBES, PH_GLOCK, PH_PIANO, PH_WOOD, PH_BELL}) {
      const bool shortBar = s == PH_MARIMBA || s == PH_WOOD || s == PH_GLOCK;
      if (held[s] > (shortBar ? -30 : -12)) {
        struck = false;
        printf("     %s: %.1f dB after 1.3 s held\n", phase_sound_name(s), held[s]);
      }
    }
    check(struck, "the struck sounds die away by themselves while the key is held, wood and the short bars by 30 dB");
    snprintf(what, sizeof(what),
             "a wood block is short (%.0f ms), a marimba longer (%.0f ms), vibes and bells ring (%.0f, %.0f ms)",
             len[PH_WOOD] * 1000, len[PH_MARIMBA] * 1000, len[PH_VIBES] * 1000, len[PH_BELL] * 1000);
    check(len[PH_WOOD] < 0.12 && len[PH_MARIMBA] > len[PH_WOOD] && len[PH_MARIMBA] < 0.8 && len[PH_VIBES] > 0.6 &&
              len[PH_BELL] > 0.6,
          what);
    snprintf(what, sizeof(what),
             "the organ and the voice hold while the key is down (%.1f, %.1f dB) and stop after (%.1f, %.1f dB)",
             held[PH_ORGAN], held[PH_VOICE], tail[PH_ORGAN], tail[PH_VOICE]);
    check(held[PH_ORGAN] > -6 && held[PH_VOICE] > -9 && tail[PH_ORGAN] < -50 && tail[PH_VOICE] < -40, what);
  }

  // Bright and Decay move the sound.
  {
    int32_t s[PH_FIELDS];
    phase_default_piece(0, s);
    s[PH_BRIGHT] = 100;
    uint8_t b[155], d[155], v[155];
    phase_voice(s, b);
    s[PH_BRIGHT] = 50;
    phase_voice(s, v);
    s[PH_DECAY] = 100;
    phase_voice(s, d);
    check(b[4 * 21 + 16] > v[4 * 21 + 16] && d[5 * 21 + 2] < v[5 * 21 + 2],
          "Bright raises a modulator's level and Decay slows the carriers' decay");
  }

  // Echo: the delay is a whole number of steps at the tempo.
  {
    int32_t s[PH_FIELDS];
    phase_default_piece(0, s);
    s[PH_TEMPO] = 120, s[PH_ECHO] = 40, s[PH_ECHO_STEPS] = 3;
    phase_apply(s);
    check(mf1_fx_get(10) == 61 && mf1_fx_get(8) == 1 && mf1_fx_get(11) == 40,
          "three steps of echo at 120 BPM is a 0.375 s delay");
  }

  // ------------------------------------------------------------ the processes

  // Phase: Two Mallets is 12 steps, holding for 4 repeats and drifting a step over 7.
  {
    PhaseProc p = proc(0);
    const int64_t c = 12 * T, hold = 4 * c, drift = 7 * c;
    PhaseView v;
    phase_view(&p, hold - 1, &v);
    bool lockedStart = v.lead_milli == 0 && !v.drifting;
    phase_view(&p, hold + drift / 2, &v);
    bool half = v.drifting && v.lead_milli == 500;
    phase_view(&p, hold + drift, &v);
    bool one = v.lead_milli == 1000 && !v.drifting && v.offset_milli == 1000;
    phase_view(&p, 2 * hold + drift - 1, &v);
    bool holds = v.lead_milli == 1000 && !v.drifting;
    check(lockedStart && half && one && holds,
          "phase: part 2 holds in unison, is half a step ahead halfway through the drift, a step ahead after it, and "
          "holds there");
    auto ns = notes(p, hold, hold + drift);
    char what[160];
    snprintf(what, sizeof(what), "phase: over the drift part 2 plays one note more than part 1 (%d against %d)",
             countPart(ns, 1), countPart(ns, 0));
    check(countPart(ns, 1) == countPart(ns, 0) + 1 && countPart(ns, 0) == 7 * 12, what);
    phase_view(&p, 12 * (hold + drift), &v);
    check(v.lead_milli == 12000 && v.offset_milli == 0, "phase: after twelve stages the parts are back in unison");
    // Exact after a long time: ten thousand stages in, the lead is ten thousand steps to the tick.
    phase_view(&p, 10000 * (hold + drift) + hold + drift / 4, &v);
    check(v.lead_milli == 10000 * 1000 + 250, "phase: positions come from time, so ten thousand stages on there is no "
                                              "rounding error");
    // During the drift part 2's steps are shorter by exactly one in 84.
    std::vector<int64_t> t2;
    for (auto& x : ns)
      if (x.part == 1) t2.push_back(x.tick);
    int64_t lo = INT64_MAX, hi = 0;
    for (size_t i = 2; i + 1 < t2.size(); i++) lo = std::min(lo, t2[i] - t2[i - 1]), hi = std::max(hi, t2[i] - t2[i - 1]);
    check(hi - lo <= 1 && std::abs((double)lo - T * 84.0 / 85) <= 1, "phase: while drifting, part 2's steps are 84/85 "
                                                                     "of part 1's");
  }

  // Shift: Wood Shift jumps a step every six repeats of twelve, with no drift between.
  {
    PhaseProc p = proc(1);
    const int64_t stage = 6 * 12 * T;
    PhaseView a, b, c;
    phase_view(&p, stage - 1, &a);
    phase_view(&p, stage, &b);
    phase_view(&p, 3 * stage + stage / 2, &c);
    check(a.lead_milli == 0 && b.lead_milli == 1000 && c.lead_milli == 3000 && !b.drifting,
          "shift: part 2 jumps a step ahead every six repeats, all at once");
    auto ns = notes(p, 0, stage);
    check(countPart(ns, 0) == countPart(ns, 1), "shift: between jumps both parts play the same number of notes");
  }

  // Build: Glass Steps fills its nine rests, one every two repeats, then empties again.
  {
    PhaseProc p = proc(2, [](int32_t* s) { s[PH_PARTS] = 1; });
    const int64_t c = 16 * T, stage = 2 * c;
    auto per = [&](int st) { return (int)notes(p, st * stage, st * stage + c).size(); };
    check(p.rests == 9 && per(0) == 7 && per(1) == 8 && per(9) == 16 && per(13) == 12 && per(18) == 7,
          "build: 7 notes, then one more every stage up to all 16, then back down to 7");
  }

  // Canon: Round of Four's voices enter two repeats apart, each four steps behind, all sing, then leave in turn.
  {
    PhaseProc p = proc(3);
    const int64_t c = 16 * T, stage = 2 * c;
    bool entries = true;
    for (int q = 0; q < 4; q++) {
      auto ns = notes(p, 0, 12 * stage);
      int64_t first = -1;
      for (auto& x : ns)
        if (x.part == q) {
          first = x.tick;
          break;
        }
      entries &= first >= q * stage && first < (q + 1) * stage;
    }
    check(entries, "canon: each voice comes in at the start of its own stage");
    int on[12];
    for (int st = 0; st < 12; st++) {
      PhaseView v;
      phase_view(&p, st * stage + stage / 2, &v);
      on[st] = 0;
      for (int q = 0; q < 4; q++) on[st] += v.part[q].active;
    }
    const int want[12] = {1, 2, 3, 4, 4, 4, 4, 4, 4, 3, 2, 1};
    check(memcmp(on, want, sizeof(on)) == 0, "canon: one, two, three, four voices, all four, then three, two, one");
    PhaseView v;
    phase_view(&p, 5 * stage, &v);
    check(v.lead_milli == -4000 && v.offset_milli == 12000, "canon: the second voice is four steps behind");
  }

  // Pulse: the chord is four notes of the cell, one along each stage, and the wave fills halfway through a stage.
  {
    PhaseProc p = proc(4);
    const int64_t stage = 4 * 8 * T;
    auto parts = [&](int64_t t) {
      PhaseView v;
      phase_view(&p, t, &v);
      int k = 0;
      for (int q = 0; q < 4; q++) k += v.part[q].active;
      return k;
    };
    check(parts(T / 2) <= 2 && parts(stage / 2) == 4 && parts(stage - T / 2) <= 2,
          "pulse: a wave starts thin, swells to all four parts and thins out again");
    PhaseView a, b;
    phase_view(&p, stage / 2, &a);
    phase_view(&p, stage + stage / 2, &b);
    check(a.part[0].note[0] == 51 && a.part[1].note[0] == 58 + 12 && b.part[0].note[0] == 58,
          "pulse: the chord moves one note along the cell each stage");
  }

  // Stretch: notes grow stage by stage, and the second part plays at half speed.
  {
    PhaseProc p = proc(5);
    const int64_t stage = 2 * 12 * T;
    PhaseView v0, v4, v8;
    phase_view(&p, 0, &v0);
    phase_view(&p, 4 * stage, &v4);
    phase_view(&p, 8 * stage, &v8);
    check(v0.part[0].len_milli[0] == 200 && v4.part[0].len_milli[0] > 2000 && v8.part[0].len_milli[0] > v4.part[0].len_milli[0],
          "stretch: a fifth of a step at first, over two steps by the fourth stage, longer still by the eighth");
    bool half = true;
    int seen = 0;
    for (auto& x : notes(p, 0, 48 * T))
      if (x.part == 1) half &= x.tick == (int64_t)x.step * 2 * T, seen++;
    check(half && seen >= 8, "stretch: the second part plays at half speed");
  }

  // Result: the third part is silent in unison and while drifting, and picks notes when the parts are locked apart.
  {
    PhaseProc p = proc(6);
    const int64_t c = 12 * T, hold = 4 * c;
    const int64_t d = (1 + (100 - 55) * 15 / 100) * c;
    auto ns0 = notes(p, 0, hold);
    auto nsDrift = notes(p, hold, hold + d);
    auto ns1 = notes(p, hold + d, 2 * hold + d);
    check(countPart(ns0, 2) == 0 && countPart(nsDrift, 2) == 0 && countPart(ns1, 2) > 0 &&
              countPart(ns1, 2) < countPart(ns1, 0),
          "result: the third part waits in unison and while drifting, then picks some notes once locked apart");
  }

  // Tape: the loops never lock; after D repeats part 2 is exactly one step on, part 3 two.
  {
    PhaseProc p = proc(7);
    const int dr = 1 + (100 - 20) * 15 / 100;
    const int64_t c = 16 * T;
    PhaseView a, b;
    phase_view(&p, dr * c, &a);
    phase_view(&p, 1000 * dr * c, &b);
    bool moving = true;
    int32_t last = -1;
    for (int k = 1; k < 40; k++) {
      PhaseView v;
      phase_view(&p, k * c / 2, &v);
      moving &= v.lead_milli > last;
      last = v.lead_milli;
    }
    check(a.lead_milli == 1000 && b.lead_milli == 1000000 && moving,
          "tape: part 2 gains a step every 13 repeats, exactly, and never stops drifting");
  }

  // Density and length.
  {
    PhaseProc full = proc(0), half = proc(0, [](int32_t* s) { s[PH_DENSITY] = 50; }),
              short8 = proc(0, [](int32_t* s) { s[PH_LENGTH] = 8; });
    int nf = 0, nh = 0;
    for (int i = 0; i < 16; i++) nf += full.note[i] >= 0, nh += half.note[i] >= 0;
    check(nf == 12 && nh == 6 && half.note[0] == full.note[0] && short8.length == 8 && short8.note[8] == -1,
          "Density 50 keeps half the notes and the first; Length cuts the cell");
  }

  // ------------------------------------------------------------ the player

  // Two parts in unison strike one voice, and the note is counted twice.
  {
    int32_t s[PH_FIELDS];
    phase_default_piece(0, s);
    s[PH_ROOM] = 0;
    PhaseCell c;
    phase_default_cell(0, &c);
    phase_load(s);
    silence();
    phase_offline(true);
    PhaseProc p;
    phase_proc_make(s, &c, &p);
    const int idle = mf1_active_voices();
    phase_start(&p, 0);
    render(0.003);
    int first = c.note[0];
    const int refs = phase_refs(first), voices = mf1_active_voices() - idle;
    phase_offline_seek(0.3);
    phase_stop();
    char what[120];
    snprintf(what, sizeof(what), "two parts in unison count the note twice (%d) but strike it once (%d voice)", refs,
             voices);
    check(refs == 2 && voices == 1, what);
  }

  // A note two parts hold stays on until both let go: Long Chord's second part holds its notes twice as long.
  {
    int32_t s[PH_FIELDS];
    phase_default_piece(5, s);
    s[PH_GATE] = 100, s[PH_LENGTH] = 2, s[PH_MOTION] = 0, s[PH_ROOM] = 0;
    PhaseCell c;
    memset(c.note, -1, sizeof(c.note));
    c.note[0] = 60, c.length = 2;
    phase_load(s);
    silence();
    phase_offline(true);
    PhaseProc p;
    phase_proc_make(s, &c, &p);
    const double step = 15.0 / s[PH_TEMPO];
    phase_start(&p, 0);
    phase_offline_seek(0.5 * step);
    bool both = phase_refs(60) == 2;
    phase_offline_seek(1.5 * step);
    render(0.01);
    uint64_t lo, hi;
    mf1_sounding(&lo, &hi);
    bool kept = phase_refs(60) == 1 && ((lo >> 60) & 1);
    // A held key shares the count too: the process letting go leaves it sounding.
    phase_key_down(60);
    phase_offline_seek(1.99 * step);
    bool key = phase_refs(60) == 2;
    phase_stop();
    bool keyOnly = phase_refs(60) == 1;
    phase_key_up(60);
    check(both && kept && key && keyOnly && phase_refs(60) == 0,
          "a note held by two parts, or a part and a key, sounds until the last one lets go");
  }

  // Stopping lets go of everything: a sustained organ is silent soon after.
  {
    int32_t s[PH_FIELDS];
    phase_default_piece(5, s);
    s[PH_ROOM] = 0, s[PH_GATE] = 100, s[PH_MOTION] = 100;
    PhaseCell c;
    phase_default_cell(5, &c);
    auto a = perform(s, c, 3.0);
    phase_stop();
    int held = 0;
    for (int k = 0; k < 128; k++) held += phase_refs(k);
    auto b = render(0.6);
    check(db(rms(a, 1, 3)) > -40 && held == 0 && db(rms(b, 0.4, 0.6)) < -80,
          "after stop no note is held and the organ falls silent");
  }

  // Timing: at 120 BPM part 1's notes start every 125 ms, to the block.
  {
    int32_t s[PH_FIELDS];
    phase_default_piece(0, s);
    s[PH_TEMPO] = 120, s[PH_PARTS] = 1, s[PH_GATE] = 50;
    PhaseCell c;
    phase_default_cell(0, &c);
    phase_load(s);
    silence();
    phase_offline(true);
    PhaseProc p;
    phase_proc_make(s, &c, &p);
    phase_start(&p, 0);
    std::vector<double> starts;
    int last = 0;
    for (double t = 0; t < 1.6; t += 0.0005) {
      phase_offline_seek(t);
      int r = 0;
      for (int k = 0; k < 128; k++) r += phase_refs(k);
      if (r > last) starts.push_back(t);
      last = r;
    }
    phase_stop();
    bool even = starts.size() >= 12;
    for (size_t i = 1; i < starts.size(); i++) even &= std::fabs(starts[i] - starts[i - 1] - 0.125) < 0.001;
    check(even, "at 120 BPM a note starts every 125 ms, within half a millisecond");
  }

  // A live tempo change keeps the time; a change to the process restarts it on the next step.
  {
    int32_t s[PH_FIELDS];
    phase_default_piece(0, s);
    PhaseCell c;
    phase_default_cell(0, &c);
    phase_offline(true);
    PhaseProc p;
    phase_proc_make(s, &c, &p);
    phase_start(&p, 0);
    phase_offline_seek(10.0);
    PhaseView before, after;
    phase_now(&before);
    s[PH_TEMPO] = 66;
    phase_proc_make(s, &c, &p);
    phase_change(&p);
    phase_now(&after);
    bool kept = std::abs(after.lead_milli - before.lead_milli) <= 1 &&
                std::abs(after.part[0].pos_milli - before.part[0].pos_milli) <= 2;
    s[PH_PROCESS] = PH_SHIFT;
    phase_proc_make(s, &c, &p);
    phase_change(&p);
    phase_offline_seek(10.0 + 2 * 15.0 / 66);
    PhaseView restarted;
    phase_now(&restarted);
    phase_stop();
    check(kept && restarted.lead_milli == 0 && restarted.part[0].pos_milli < 2000,
          "a tempo change keeps the parts where they are; a new process starts again from unison");
  }

  phase_shutdown();
  phase_offline(false);
  silence();
  return failures;
}
