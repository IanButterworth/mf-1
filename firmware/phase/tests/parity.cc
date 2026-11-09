// Checks the firmware's C port of Phase against MF-1's own C++ (Sources/PhaseCore): every piece's settings give the
// desktop app's voice bytes and effect settings and order, and the firmware's player, run on the engine's blocks,
// starts and releases the same notes at the same times as the desktop's player, run offline. The processes are the
// desktop's own process.c in both, which the parity script checks is copied unchanged. The engine itself is
// iScream's port, which firmware/iscream/tests/run_parity.sh checks against MF-1's.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "MF1Core.h"
#include "fx.h"

extern "C" {
#include "../../iscream/dsp/fx.h"
#include "../dsp/ph.h"
#include "../dsp/player.h"
// The device's 64-bit divisions (dsp/div64.c), built under these names for the test.
int64_t ph_test_divdi3(int64_t a, int64_t b);
int64_t ph_test_moddi3(int64_t a, int64_t b);
}

static int failures = 0;
static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

// ---------------------------------------------------------------- the desktop's engine calls, captured

struct Event {
  long sample;  // when it happened, in samples from the start
  int note;
  bool on;
};

static uint8_t gEdit[156];
static mf1::FxParams gFx;
static bool gMono, gArpOn;
static int gTempo;
static long gNow;  // the desktop player's time, in samples
static std::vector<Event> gDesk, gFirm;

extern "C" {
int mf1_fx_get(int cc) { return cc == 3 ? gFx.filterQ : gFx.get(cc); }
void mf1_fx_set(int cc, int v) {
  if (cc == 3)
    gFx.filterQ = std::max(0, std::min(100, v));
  else
    gFx.set(cc, v);
}
int mf1_edit_get(int i) { return gEdit[i]; }
void mf1_edit_set(int i, int v) { gEdit[i] = (uint8_t)v; }
bool mf1_edit_mono(void) { return gMono; }
void mf1_edit_set_mono(bool m) { gMono = m; }
int mf1_env_get(int) { return 0; }
void mf1_env_set(int, int) {}
int mf1_fx_order(int pos) { return gFx.order[pos]; }
void mf1_fx_swap(int a, int b) { std::swap(gFx.order[a], gFx.order[b]); }
void mf1_load_preset(int) {}
bool mf1_import_factory_bank(int, const uint8_t*, size_t) { return true; }
bool mf1_arp_on(void) { return gArpOn; }
void mf1_arp_enable(bool on) { gArpOn = on; }
int mf1_global_get(int) { return gTempo; }
void mf1_global_set(int, int v) { gTempo = v; }
void mf1_key_down(int note) { gDesk.push_back({gNow, note, true}); }
void mf1_key_up(int note) { gDesk.push_back({gNow, note, false}); }
void mf1_all_notes_off(void) {}

// The firmware player's side: the counts the mode's core keeps.
static int gRefs[128];
static long gBlock;
int ph_refs(int note) { return gRefs[note]; }
void ph_hold(int note, int strike) {
  if (strike || gRefs[note] == 0) gFirm.push_back({gBlock * PH_BLOCK, note, true});
  gRefs[note]++;
}
void ph_let_go(int note) {
  if (gRefs[note] > 0 && --gRefs[note] == 0) gFirm.push_back({gBlock * PH_BLOCK, note, false});
}
}

// ---------------------------------------------------------------- the players side by side

// Each desktop event against a firmware one with the same note, both strikes or both releases: the firmware does at
// the start of a block what falls due within it, the desktop's offline player when it is moved to the block's end.
static int unmatched(std::vector<Event> desk, std::vector<Event> firm, int* worst) {
  std::vector<bool> used(firm.size());
  int missing = 0;
  size_t from = 0;
  *worst = 0;
  for (const Event& d : desk) {
    while (from < firm.size() && firm[from].sample < d.sample - 2 * PH_BLOCK) from++;
    bool found = false;
    for (size_t i = from; i < firm.size() && firm[i].sample <= d.sample + 2 * PH_BLOCK; i++) {
      if (used[i] || firm[i].note != d.note || firm[i].on != d.on) continue;
      used[i] = true;
      found = true;
      *worst = std::max(*worst, (int)std::labs(d.sample - firm[i].sample));
      break;
    }
    if (!found && missing++ < 3)
      printf("     the desktop's %s of note %d at sample %ld has no match\n", d.on ? "strike" : "release", d.note,
             d.sample);
  }
  for (size_t i = 0, shown = 0; i < firm.size() && shown < 3; i++)
    if (!used[i])
      printf("     the firmware's %s of note %d at sample %ld has no match\n", firm[i].on ? "strike" : "release",
             firm[i].note, firm[i].sample),
          shown++;
  return missing + (int)(firm.size() - (desk.size() - missing));
}

struct Step {
  long at;  // block
  int what;  // 0 change, 1 stop
  PhaseProc proc;
};

// Plays a process on both players for `blocks` blocks, applying the steps at their blocks, then stops both.
static void play_both(const PhaseProc& start, long blocks, const std::vector<Step>& steps) {
  gDesk.clear(), gFirm.clear();
  phase_offline(true);
  gNow = 0;
  phase_start(&start, 0);
  gBlock = 0;
  ph_player_start(&start);
  for (long k = 0; k < blocks; k++) {
    for (const Step& s : steps)
      if (s.at == k) {
        if (s.what == 0)
          phase_change(&s.proc), ph_player_change(&s.proc);
        else
          phase_stop(), ph_player_stop();
      }
    gBlock = k;
    ph_player_block();
    gNow = (k + 1) * PH_BLOCK;
    phase_offline_seek((double)gNow / PH_RATE);
  }
  phase_stop();
  ph_player_stop();
  gBlock = blocks;
  ph_player_block();
  phase_offline(false);
}

static PhaseProc make(int piece, const int32_t* s = nullptr) {
  int32_t d[PH_FIELDS];
  if (s)
    memcpy(d, s, sizeof d);
  else
    ph_default_piece(piece, d);
  PhaseCell c;
  ph_default_cell(piece, &c);
  PhaseProc p;
  phase_proc_make(d, &c, &p);
  return p;
}

int main() {
  const int n = phase_piece_count();
  check(n == ph_piece_count(), "same number of pieces");
  int names = 0, ranges = 0, defaults = 0, cells = 0, voice = 0, fx = 0, settings = 0;
  for (int field = 0; field < PH_FIELDS; field++) {
    int lo, hi, lo2, hi2;
    phase_range(field, &lo, &hi);
    ph_range(field, &lo2, &hi2);
    ranges += lo != lo2 || hi != hi2;
  }
  for (int i = -1; i <= PH_PROCESS_COUNT; i++) names += strcmp(phase_process_name(i), ph_process_name(i)) != 0;
  for (int i = -1; i <= PH_SOUND_COUNT; i++) names += strcmp(phase_sound_name(i), ph_sound_name(i)) != 0;
  for (int i = -1; i <= PH_SPREAD_COUNT; i++) names += strcmp(phase_spread_name(i), ph_spread_name(i)) != 0;

  auto fx_differs = [](const int32_t* s) {
    phase_apply(s);
    isc_fx_params_t p;
    ph_fx(s, &p);
    int d = 0;
    for (int cc = 0; cc < 24; cc++) d += mf1_fx_get(cc) != (cc == 3 ? p.cc[3] : isc_fx_params_get(&p, cc));
    for (int pos = 0; pos < 6; pos++) d += gFx.order[pos] != p.order[pos];
    return d;
  };

  for (int pc = 0; pc < n; pc++) {
    PhasePiece a;
    phase_piece(pc, &a);
    const ph_piece_t* b = ph_piece(pc);
    names += strcmp(a.name, b->voice_name) || strcmp(a.title, b->title) || strcmp(a.blurb, b->blurb);
    int32_t base[PH_FIELDS], mine[PH_FIELDS];
    phase_default_piece(pc, base);
    ph_default_piece(pc, mine);
    defaults += memcmp(base, mine, sizeof base) != 0;
    PhaseCell ca, cb;
    phase_default_cell(pc, &ca);
    ph_default_cell(pc, &cb);
    cells += memcmp(&ca, &cb, sizeof ca) != 0;
    // Every value of every field, one field at a time, from the piece as written.
    for (int field = 0; field < PH_FIELDS; field++) {
      int lo, hi;
      phase_range(field, &lo, &hi);
      for (int v = lo - 1; v <= hi + 1; v++) {
        int32_t s[PH_FIELDS];
        memcpy(s, base, sizeof s);
        s[field] = v;
        uint8_t va[155], vb[155];
        phase_voice(s, va);
        ph_voice(s, vb);
        if (memcmp(va, vb, 155) && voice++ < 5)
          for (int i = 0; i < 155; i++)
            if (va[i] != vb[i]) printf("     piece %d field %d = %d: byte %d is %d, C gives %d\n", pc, field, v, i, va[i], vb[i]);
        fx += fx_differs(s);
        settings++;
      }
    }
  }
  // The echo's time for every tempo and number of steps together.
  int echo = 0;
  for (int t = 40; t <= 200; t++)
    for (int e = 1; e <= 8; e++) {
      int32_t s[PH_FIELDS];
      ph_default_piece(7, s);
      s[PH_TEMPO] = t, s[PH_ECHO_STEPS] = e;
      int d = fx_differs(s);
      if (d && echo < 5) printf("     tempo %d, %d steps: the echo differs\n", t, e);
      echo += d != 0;
    }

  char what[200];
  snprintf(what, sizeof what, "names, titles and ranges match PhaseCore (%d and %d differ)", names, ranges);
  check(names == 0 && ranges == 0, what);
  snprintf(what, sizeof what, "every piece and cell as written matches (%d and %d differ)", defaults, cells);
  check(defaults == 0 && cells == 0, what);
  snprintf(what, sizeof what, "%d settings give PhaseCore's voice bytes (%d differ)", settings, voice);
  check(voice == 0, what);
  snprintf(what, sizeof what, "and its effect settings and order (%d differ)", fx);
  check(fx == 0, what);
  snprintf(what, sizeof what, "the echo's time for every tempo and step count (%d of %d differ)", echo, 161 * 8);
  check(echo == 0, what);

  // The players: each piece for a minute as written, then with four parts and every span.
  const long minute = 60L * PH_RATE / PH_BLOCK;
  int events = 0, missing = 0, worst = 0;
  for (int pc = 0; pc < n; pc++)
    for (int variant = 0; variant < 2; variant++) {
      int32_t s[PH_FIELDS];
      ph_default_piece(pc, s);
      if (variant) s[PH_PARTS] = 4, s[PH_SPREAD] = pc % PH_SPREAD_COUNT, s[PH_DENSITY] = 70;
      play_both(make(pc, s), minute, {});
      int w;
      int m = unmatched(gDesk, gFirm, &w);
      if (m) printf("     piece %d%s: %d of %zu events differ\n", pc, variant ? " (four parts)" : "", m, gDesk.size());
      missing += m, events += (int)gDesk.size(), worst = std::max(worst, w);
    }
  snprintf(what, sizeof what,
           "the player starts and releases the desktop's %d notes, a block apart at most (%d differ, worst %d samples)",
           events, missing, worst);
  check(missing == 0 && worst <= PH_BLOCK, what);

  // Changes while playing: the tempo (the time kept), how the parts move (a restart on the next step), the notes,
  // then a stop.
  {
    int32_t s[PH_FIELDS];
    ph_default_piece(0, s);
    std::vector<Step> steps;
    s[PH_TEMPO] = 150;
    steps.push_back({(long)(10 * PH_RATE / PH_BLOCK) + 3, 0, make(0, s)});
    s[PH_PROCESS] = PH_SHIFT;
    steps.push_back({(long)(20 * PH_RATE / PH_BLOCK) + 7, 0, make(0, s)});
    s[PH_TRANSPOSE] = -5, s[PH_GATE] = 90;
    steps.push_back({(long)(25 * PH_RATE / PH_BLOCK) + 1, 0, make(0, s)});
    steps.push_back({(long)(30 * PH_RATE / PH_BLOCK), 1, {}});
    play_both(make(0), 35L * PH_RATE / PH_BLOCK, steps);
    int w, m = unmatched(gDesk, gFirm, &w);
    snprintf(what, sizeof what, "and through changes of tempo, process and notes, and a stop (%d of %zu differ)", m,
             gDesk.size());
    check(m == 0 && w <= PH_BLOCK, what);
  }
  // The device's divisions, on numbers of every size and sign.
  {
    uint64_t x = 88172645463325252ull;
    int wrong = 0, tried = 0;
    auto next = [&x] { return x ^= x << 13, x ^= x >> 7, x ^= x << 17; };
    for (int i = 0; i < 400000; i++) {
      int64_t a = (int64_t)(next() >> (next() % 64)), b = (int64_t)(next() >> (next() % 64));
      if (next() & 1) a = -a;
      if (next() & 1) b = -b;
      if (b == 0 || (a == INT64_MIN && b == -1)) continue;
      wrong += ph_test_divdi3(a, b) != a / b || ph_test_moddi3(a, b) != a % b;
      tried++;
    }
    snprintf(what, sizeof what, "the device's 64-bit division and remainder are C's (%d of %d differ)", wrong, tried);
    check(wrong == 0, what);
  }
  int held = 0;
  for (int i = 0; i < 128; i++) held += gRefs[i] + phase_refs(i);
  snprintf(what, sizeof what, "a stop lets go of every note (%d still held)", held + ph_player_held());
  check(held == 0 && ph_player_held() == 0, what);

  printf(failures ? "%d failed\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
