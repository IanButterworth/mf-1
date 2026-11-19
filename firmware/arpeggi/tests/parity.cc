// Checks the firmware's C port of Arpeggi against MF-1's own C++ (Sources/ArpeggiCore): every scene's settings give
// the desktop app's voice bytes and effect settings and order, and the firmware's player, run on the engine's blocks,
// starts and releases the same notes at the same times as the desktop's player, run offline, through keys, changes
// and stops. The pool and the lanes are the desktop's own arp.c in both, which the parity script checks is copied
// unchanged. The engine itself is iScream's port, which firmware/iscream/tests/run_parity.sh checks against MF-1's.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "MF1Core.h"
#include "fx.h"

extern "C" {
#include "../../iscream/dsp/fx.h"
#include "../dsp/ap.h"
#include "../dsp/ap_player.h"
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
int ap_refs(int note) { return gRefs[note]; }
void ap_hold(int note, int strike) {
  if (strike || gRefs[note] == 0) gFirm.push_back({gBlock * AP_BLOCK, note, true});
  gRefs[note]++;
}
void ap_let_go(int note) {
  if (gRefs[note] > 0 && --gRefs[note] == 0) gFirm.push_back({gBlock * AP_BLOCK, note, false});
}
}

// ---------------------------------------------------------------- the players side by side

// Each desktop event against a firmware one with the same note, both strikes or both releases: the firmware does at
// the start of a block what falls due within it, the desktop's offline player when it is moved to the block's end.
static int unmatched(const std::vector<Event>& desk, const std::vector<Event>& firm, int* worst) {
  std::vector<bool> used(firm.size());
  int missing = 0;
  size_t from = 0;
  *worst = 0;
  for (const Event& d : desk) {
    while (from < firm.size() && firm[from].sample < d.sample - 2 * AP_BLOCK) from++;
    bool found = false;
    for (size_t i = from; i < firm.size() && firm[i].sample <= d.sample + 2 * AP_BLOCK; i++) {
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

// Something both players are told at the start of a block.
struct Step {
  long at;
  std::function<void()> desk, firm;
};

static ApProc make(const int32_t* s) {
  ApProc p;
  ap_proc_make(s, &p);
  return p;
}

// Plays both players for `blocks` blocks from a stop, doing each step at its block, then stops both.
static void play_both(const int32_t* s, long blocks, const std::vector<Step>& steps) {
  gDesk.clear(), gFirm.clear();
  ApProc p = make(s);
  arpeggi_offline(true);
  gNow = 0, gBlock = 0;
  arpeggi_clear();
  arpeggi_set(&p);
  arpeggi_pool_mode(s[AP_HOLD_MODE], s[AP_RING_SIZE]);
  ap_player_reset();
  ap_player_set(&p);
  ap_player_mode(s[AP_HOLD_MODE], s[AP_RING_SIZE]);
  for (long k = 0; k < blocks; k++) {
    gNow = k * AP_BLOCK;
    for (const Step& st : steps)
      if (st.at == k) st.desk(), st.firm();
    gBlock = k;
    ap_player_block();
    gNow = (k + 1) * AP_BLOCK;
    arpeggi_offline_seek((double)gNow / AP_RATE);
  }
  arpeggi_stop();
  ap_player_stop();
  gBlock = blocks;
  ap_player_block();
  arpeggi_offline(false);
}

static Step seed(long at, int scene) {
  return {at,
          [scene] {
            int8_t n[AP_MAX_POOL];
            int k = arpeggi_default_seed(scene, n);
            arpeggi_seed(n, k);
          },
          [scene] {
            int8_t n[AP_MAX_POOL];
            int k = ap_default_seed(scene, n);
            ap_player_seed(n, k);
          }};
}

static Step down(long at, int note) { return {at, [note] { arpeggi_key_down(note); }, [note] { ap_player_key_down(note); }}; }
static Step up(long at, int note) { return {at, [note] { arpeggi_key_up(note); }, [note] { ap_player_key_up(note); }}; }

int main() {
  const int n = arpeggi_scene_count();
  check(n == ap_scene_count(), "same number of scenes");
  int names = 0, ranges = 0, defaults = 0, seeds = 0, voice = 0, fx = 0, settings = 0;
  for (int field = 0; field < AP_FIELDS; field++) {
    int lo, hi, lo2, hi2;
    arpeggi_range(field, &lo, &hi);
    ap_range(field, &lo2, &hi2);
    ranges += lo != lo2 || hi != hi2;
  }
  for (int i = -1; i <= AP_SHAPE_COUNT; i++) names += strcmp(arpeggi_shape_name(i), ap_shape_name(i)) != 0;
  for (int i = -1; i <= AP_RATE_COUNT; i++) names += strcmp(arpeggi_rate_name(i), ap_rate_name(i)) != 0;
  for (int i = -1; i <= AP_RHYTHM_COUNT; i++) names += strcmp(arpeggi_rhythm_name(i), ap_rhythm_name(i)) != 0;
  for (int i = -1; i <= AP_RELATION_COUNT; i++) names += strcmp(arpeggi_relation_name(i), ap_relation_name(i)) != 0;
  for (int i = -1; i <= AP_HOLD_COUNT; i++) names += strcmp(arpeggi_hold_name(i), ap_hold_name(i)) != 0;
  for (int i = -1; i <= AP_SOUND_COUNT; i++) names += strcmp(arpeggi_sound_name(i), ap_sound_name(i)) != 0;

  auto fx_differs = [](const int32_t* s) {
    arpeggi_apply(s);
    isc_fx_params_t p;
    ap_fx(s, &p);
    int d = 0;
    for (int cc = 0; cc < 24; cc++) d += mf1_fx_get(cc) != (cc == 3 ? p.cc[3] : isc_fx_params_get(&p, cc));
    for (int pos = 0; pos < 6; pos++) d += gFx.order[pos] != p.order[pos];
    return d;
  };

  for (int sc = 0; sc < n; sc++) {
    ApScene a;
    arpeggi_scene(sc, &a);
    const ap_scene_t* b = ap_scene(sc);
    names += strcmp(a.name, b->voice_name) || strcmp(a.title, b->title) || strcmp(a.blurb, b->blurb);
    int32_t base[AP_FIELDS], mine[AP_FIELDS];
    arpeggi_default_scene(sc, base);
    ap_default_scene(sc, mine);
    defaults += memcmp(base, mine, sizeof base) != 0;
    int8_t sa[AP_MAX_POOL], sb[AP_MAX_POOL];
    int na = arpeggi_default_seed(sc, sa), nb = ap_default_seed(sc, sb);
    seeds += na != nb || memcmp(sa, sb, na) != 0;
    // Every value of every field, one field at a time, from the scene as written.
    for (int field = 0; field < AP_FIELDS; field++) {
      int lo, hi;
      arpeggi_range(field, &lo, &hi);
      for (int v = lo - 1; v <= hi + 1; v++) {
        int32_t s[AP_FIELDS];
        memcpy(s, base, sizeof s);
        s[field] = v;
        uint8_t va[155], vb[155];
        arpeggi_voice(s, va);
        ap_voice(s, vb);
        if (memcmp(va, vb, 155) && voice++ < 5)
          for (int i = 0; i < 155; i++)
            if (va[i] != vb[i])
              printf("     scene %d field %d = %d: byte %d is %d, C gives %d\n", sc, field, v, i, va[i], vb[i]);
        fx += fx_differs(s);
        settings++;
      }
    }
  }
  // The echo's time for every tempo and number of steps together.
  int echo = 0;
  for (int t = 40; t <= 240; t++)
    for (int e = 1; e <= 8; e++) {
      int32_t s[AP_FIELDS];
      ap_default_scene(2, s);
      s[AP_TEMPO] = t, s[AP_ECHO_STEPS] = e;
      int d = fx_differs(s);
      if (d && echo < 5) printf("     tempo %d, %d steps: the echo differs\n", t, e);
      echo += d != 0;
    }

  char what[200];
  snprintf(what, sizeof what, "names, titles and ranges match ArpeggiCore (%d and %d differ)", names, ranges);
  check(names == 0 && ranges == 0, what);
  snprintf(what, sizeof what, "every scene and chord as written matches (%d and %d differ)", defaults, seeds);
  check(defaults == 0 && seeds == 0, what);
  snprintf(what, sizeof what, "%d settings give ArpeggiCore's voice bytes (%d differ)", settings, voice);
  check(voice == 0, what);
  snprintf(what, sizeof what, "and its effect settings and order (%d differ)", fx);
  check(fx == 0, what);
  snprintf(what, sizeof what, "the echo's time for every tempo and step count (%d of %d differ)", echo, 201 * 8);
  check(echo == 0, what);

  // The players: each scene for a minute with its own chord, then with every shape, rate and rhythm on both lanes
  // in turn, through every relation.
  const long minute = 60L * AP_RATE / AP_BLOCK;
  int events = 0, missing = 0, worst = 0;
  auto run = [&](const int32_t* s, long blocks, const std::vector<Step>& steps, const char* what) {
    play_both(s, blocks, steps);
    int w, m = unmatched(gDesk, gFirm, &w);
    if (m || w > AP_BLOCK) printf("     %s: %d of %zu events differ, worst %d samples\n", what, m, gDesk.size(), w);
    missing += m, events += (int)gDesk.size(), worst = std::max(worst, w);
  };
  for (int sc = 0; sc < n; sc++) {
    int32_t s[AP_FIELDS];
    ap_default_scene(sc, s);
    char name[64];
    snprintf(name, sizeof name, "scene %d", sc);
    run(s, minute, {seed(0, sc)}, name);
  }
  for (int i = 0; i < 12; i++) {
    int32_t s[AP_FIELDS];
    ap_default_scene(i % n, s);
    s[AP_A_SHAPE] = i % AP_SHAPE_COUNT, s[AP_B_SHAPE] = (i * 5 + 3) % AP_SHAPE_COUNT;
    s[AP_A_RATE] = i % AP_RATE_COUNT, s[AP_B_RATE] = (i * 7 + 2) % AP_RATE_COUNT;
    s[AP_A_RHYTHM] = i % AP_RHYTHM_COUNT, s[AP_B_RHYTHM] = (i * 4 + 1) % AP_RHYTHM_COUNT;
    s[AP_RELATION] = i % AP_RELATION_COUNT, s[AP_DUO] = 1, s[AP_SPAN] = 1 + i % 4, s[AP_SWING] = i * 9;
    s[AP_A_GATE] = 20 + i * 15, s[AP_TEMPO] = 60 + i * 15;
    char name[64];
    snprintf(name, sizeof name, "variant %d", i);
    run(s, 20L * AP_RATE / AP_BLOCK, {seed(0, i % n)}, name);
  }
  snprintf(what, sizeof what,
           "the player starts and releases the desktop's %d notes, a block apart at most (%d differ, worst %d samples)",
           events, missing, worst);
  check(missing == 0 && worst <= AP_BLOCK, what);

  // Keys, changes and stops: a ring filled and written over, Hold let go and pressed again, Latch's new chord, a
  // tempo change, a stop and a start, and a clear.
  {
    const long sec = AP_RATE / AP_BLOCK;
    events = missing = worst = 0;
    int32_t s[AP_FIELDS];
    ap_default_scene(0, s);
    std::vector<Step> st;
    const int ring[] = {60, 62, 64, 67, 69, 71, 72, 74};
    for (int i = 0; i < 8; i++) st.push_back(down(i * sec / 3 + 1, ring[i])), st.push_back(up(i * sec / 3 + 9, ring[i]));
    int32_t t[AP_FIELDS];
    memcpy(t, s, sizeof t);
    t[AP_TEMPO] = 150;
    ApProc pt = make(t);
    st.push_back({5 * sec + 3, [pt] { arpeggi_set(&pt); }, [pt] { ap_player_set(&pt); }});
    st.push_back({6 * sec, [] { arpeggi_pool_mode(AP_RING, 3); }, [] { ap_player_mode(AP_RING, 3); }});
    st.push_back({7 * sec, [] { arpeggi_stop(); }, [] { ap_player_stop(); }});
    st.push_back({8 * sec, [] { arpeggi_start(); }, [] { ap_player_start(); }});
    st.push_back({9 * sec, [] { arpeggi_clear(); }, [] { ap_player_clear(); }});
    run(s, 10 * sec, st, "ring");

    ap_default_scene(3, s);
    st.clear();
    st.push_back(down(5, 57)), st.push_back(down(9, 64)), st.push_back(up(sec, 57)), st.push_back(up(sec + 40, 64));
    st.push_back(down(2 * sec, 60)), st.push_back(down(2 * sec + 2, 67)), st.push_back(up(3 * sec + 11, 67));
    st.push_back(up(4 * sec, 60));
    run(s, 5 * sec, st, "hold");

    ap_default_scene(2, s);
    st.clear();
    st.push_back(down(3, 53)), st.push_back(down(4, 60)), st.push_back(up(sec / 2, 53)), st.push_back(up(sec / 2, 60));
    st.push_back(down(3 * sec, 58)), st.push_back(down(3 * sec + 30, 65)), st.push_back(up(3 * sec + 60, 58));
    st.push_back(up(3 * sec + 61, 65));
    run(s, 6 * sec, st, "latch");
    snprintf(what, sizeof what, "and through keys, rings, Hold, Latch, a tempo change, stops and a clear (%d of %d differ)",
             missing, events);
    check(missing == 0 && worst <= AP_BLOCK, what);
  }

  int held = 0;
  for (int i = 0; i < 128; i++) held += gRefs[i] + arpeggi_refs(i);
  snprintf(what, sizeof what, "a stop lets go of every note (%d still held)", held + ap_player_held());
  check(held == 0 && ap_player_held() == 0, what);

  printf(failures ? "%d failed\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
