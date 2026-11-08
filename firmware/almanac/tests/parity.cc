// Checks the firmware's C port of Almanac against MF-1's own C++ (Sources/AlmanacCore): every day gives the desktop
// app's voice bytes, effect settings and order, pattern settings and key notes. The engine itself is iScream's port,
// which firmware/iscream/tests/run_parity.sh checks against MF-1's.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MF1Core.h"
#include "fx.h"

// The firmware's copy of AlmanacCore.h, which the parity script checks is the desktop's.
extern "C" {
#include "../../iscream/dsp/fx.h"
#include "../dsp/alm.h"
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
  const int n = almanac_month_count();
  check(n == alm_month_count(), "same number of months");
  int names = 0, defaults = 0, ranges = 0, voice = 0, fx = 0, pattern = 0, keys = 0, days = 0;
  for (int field = 0; field < AL_FIELDS; field++) {
    int lo, hi, lo2, hi2;
    almanac_range(field, &lo, &hi);
    alm_range(field, &lo2, &hi2);
    ranges += lo != lo2 || hi != hi2;
  }
  for (int i = 0; i < 101; i++) names += strcmp(almanac_mood_name(i), alm_mood_name(i)) != 0;
  for (int p = 0; p < AL_PLACE_COUNT; p++) names += strcmp(almanac_place_name(p), alm_place_name(p)) != 0;
  for (int k = 0; k < AL_SKY_COUNT; k++) names += strcmp(almanac_sky_name(k), alm_sky(k)->name) != 0;
  for (int m = 0; m < n; m++) {
    AlmanacMonth a;
    almanac_month(m, &a);
    const alm_month_t* b = alm_month(m);
    names += strcmp(a.name, b->voice_name) || strcmp(a.month, b->month) || strcmp(a.title, b->title) ||
             strcmp(a.blurb, b->blurb) || a.skyTop != b->sky_top || a.ground != b->ground || a.accent != b->accent ||
             a.season != b->season || a.mono != (b->mono != 0);
    int32_t base[AL_FIELDS], mine[AL_FIELDS];
    almanac_default_day(m, base);
    alm_default_day(m, mine);
    defaults += memcmp(base, mine, sizeof base) != 0;
    // Every value of every field, one field at a time, from the month as composed.
    for (int field = 1; field < AL_FIELDS; field++) {
      int lo, hi;
      almanac_range(field, &lo, &hi);
      for (int v = lo; v <= hi; v++) {
        int32_t s[AL_FIELDS];
        memcpy(s, base, sizeof s);
        s[field] = v;
        uint8_t va[155], vb[155];
        almanac_voice(s, va);
        alm_voice(s, vb);
        if (memcmp(va, vb, 155) && voice++ < 5)
          for (int i = 0; i < 155; i++)
            if (va[i] != vb[i]) printf("     month %d field %d = %d: byte %d is %d, C gives %d\n", m, field, v, i, va[i], vb[i]);
        almanac_apply(s);
        isc_fx_params_t p;
        alm_fx(s, &p);
        for (int cc = 0; cc < 24; cc++)
          if (mf1_fx_get(cc) != (cc == 3 ? p.cc[3] : isc_fx_params_get(&p, cc))) fx++;
        for (int pos = 0; pos < 6; pos++) fx += gFx.order[pos] != p.order[pos];
        alm_pattern_t pt;
        alm_pattern(s, &pt);
        pattern += pt.on != (int)gArpOn || pt.tempo != gTempo;
        if (pt.on)
          pattern += pt.mode != gArp[MF1_A_MODE] || pt.octaves != gArp[MF1_A_OCTAVES] || pt.rate != gArp[MF1_A_RATE] ||
                     pt.gate != gArp[MF1_A_GATE];
        for (int note = 40; note < 90; note += 7)
          for (int left = 0; left < 2; left++) {
            int ka[8], kb[8];
            int ca = almanac_key_notes(s, note, left, ka), cb = alm_key_notes(s, note, left, kb);
            keys += ca != cb || memcmp(ka, kb, sizeof(int) * (size_t)std::max(0, ca)) != 0;
          }
        days++;
      }
    }
  }
  char what[160];
  snprintf(what, sizeof what, "names, colours and ranges match AlmanacCore (%d and %d differ)", names, ranges);
  check(names == 0 && ranges == 0, what);
  snprintf(what, sizeof what, "every month's day as composed matches (%d differ)", defaults);
  check(defaults == 0, what);
  snprintf(what, sizeof what, "%d days give AlmanacCore's voice bytes (%d differ)", days, voice);
  check(voice == 0, what);
  snprintf(what, sizeof what, "and its effect settings and order (%d differ)", fx);
  check(fx == 0, what);
  snprintf(what, sizeof what, "and its pattern settings (%d differ)", pattern);
  check(pattern == 0, what);
  snprintf(what, sizeof what, "and the notes each key plays (%d differ)", keys);
  check(keys == 0, what);
  printf(failures ? "%d failed\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
