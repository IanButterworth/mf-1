// Checks the firmware's C port of Woof against MF-1's own C++ (Sources/WoofCore): every dog's settings give the
// desktop app's voice bytes, effect settings and order, repeat settings and key notes. The engine itself is iScream's
// port, which firmware/iscream/tests/run_parity.sh checks against MF-1's.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MF1Core.h"
#include "fx.h"

// The firmware's copy of WoofCore.h, which the parity script checks is the desktop's.
extern "C" {
#include "../../iscream/dsp/fx.h"
#include "../dsp/wf.h"
}

static int failures = 0;
static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

// ---------------------------------------------------------------- the desktop's engine calls, captured

static uint8_t gEdit[156];
static mf1::FxParams gFx;
static bool gMono, gArpOn;
static int gArp[8], gTempo;
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
int mf1_arp_get(int id) { return gArp[id]; }
void mf1_arp_set(int id, int v) { gArp[id] = v; }
bool mf1_arp_on(void) { return gArpOn; }
void mf1_arp_enable(bool on) { gArpOn = on; }
int mf1_global_get(int) { return gTempo; }
void mf1_global_set(int, int v) { gTempo = v; }
}

int main() {
  const int n = woof_dog_count();
  check(n == wf_dog_count(), "same number of dogs");
  int names = 0, defaults = 0, ranges = 0, voice = 0, fx = 0, pattern = 0, keys = 0, settings = 0;
  for (int field = 0; field < WF_FIELDS; field++) {
    int lo, hi, lo2, hi2;
    woof_range(field, &lo, &hi);
    wf_range(field, &lo2, &hi2);
    ranges += lo != lo2 || hi != hi2;
  }
  for (int i = 0; i < 101; i++) names += strcmp(woof_mood_name(i), wf_mood_name(i)) != 0;
  for (int p = 0; p < WF_PLACE_COUNT; p++) names += strcmp(woof_place_name(p), wf_place_name(p)) != 0;
  for (int d = 0; d < n; d++) {
    WoofDog a;
    woof_dog(d, &a);
    const wf_breed_t* b = wf_breed(d);
    names += strcmp(a.name, b->voice_name) || strcmp(a.breed, b->breed) || strcmp(a.word, b->word) ||
             strcmp(a.blurb, b->blurb) || a.accent != b->accent || a.mono != (b->mono != 0);
    int32_t base[WF_FIELDS], mine[WF_FIELDS];
    woof_default_dog(d, base);
    wf_default_dog(d, mine);
    defaults += memcmp(base, mine, sizeof base) != 0;
    // Every value of every field, one field at a time, from the dog as bred.
    for (int field = 1; field < WF_FIELDS; field++) {
      int lo, hi;
      woof_range(field, &lo, &hi);
      for (int v = lo; v <= hi; v++) {
        int32_t s[WF_FIELDS];
        memcpy(s, base, sizeof s);
        s[field] = v;
        uint8_t va[155], vb[155];
        woof_voice(s, va);
        wf_voice(s, vb);
        if (memcmp(va, vb, 155) && voice++ < 5)
          for (int i = 0; i < 155; i++)
            if (va[i] != vb[i]) printf("     dog %d field %d = %d: byte %d is %d, C gives %d\n", d, field, v, i, va[i], vb[i]);
        woof_apply(s);
        isc_fx_params_t p;
        wf_fx(s, &p);
        for (int cc = 0; cc < 24; cc++)
          if (mf1_fx_get(cc) != (cc == 3 ? p.cc[3] : isc_fx_params_get(&p, cc))) fx++;
        for (int pos = 0; pos < 6; pos++) fx += gFx.order[pos] != p.order[pos];
        wf_pattern_t pt;
        wf_pattern(s, &pt);
        pattern += pt.on != (int)gArpOn || pt.tempo != gTempo;
        if (pt.on)
          pattern += pt.mode != gArp[MF1_A_MODE] || pt.octaves != gArp[MF1_A_OCTAVES] || pt.rate != gArp[MF1_A_RATE] ||
                     pt.gate != gArp[MF1_A_GATE];
        for (int note = 40; note < 90; note += 7) {
          int ka[8], kb[8];
          int ca = woof_key_notes(s, note, ka), cb = wf_key_notes(s, note, kb);
          keys += ca != cb || memcmp(ka, kb, sizeof(int) * (size_t)std::max(0, ca)) != 0;
        }
        settings++;
      }
    }
  }
  char what[160];
  snprintf(what, sizeof what, "names, colours and ranges match WoofCore (%d and %d differ)", names, ranges);
  check(names == 0 && ranges == 0, what);
  snprintf(what, sizeof what, "every dog as bred matches (%d differ)", defaults);
  check(defaults == 0, what);
  snprintf(what, sizeof what, "%d settings give WoofCore's voice bytes (%d differ)", settings, voice);
  check(voice == 0, what);
  snprintf(what, sizeof what, "and its effect settings and order (%d differ)", fx);
  check(fx == 0, what);
  snprintf(what, sizeof what, "and its repeat settings (%d differ)", pattern);
  check(pattern == 0, what);
  snprintf(what, sizeof what, "and the notes each key plays (%d differ)", keys);
  check(keys == 0, what);
  printf(failures ? "%d failed\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
