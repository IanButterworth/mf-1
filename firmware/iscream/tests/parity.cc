// Checks the firmware's C engine against MF-1's own C++ at 44.1 kHz:
//  - every factory voice renders the same samples in both, bit for bit,
//  - every sundae gives the desktop app's voice bytes, effect settings and effect order,
//  - each effect gives MF-1's output within float rounding.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "dsp/dx7_voice.h"
#include "fx.h"
#include "IScreamCore.h"

extern "C" {
#include "../dsp/fm.h"
#include "../dsp/fx.h"
#include "../dsp/iscream.h"
}

static int failures = 0;
static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

// ---------------------------------------------------------------- the desktop's engine calls, captured

// These behave as MF-1's engine does: Q (cc 3) is taken at the panel's 0..100.
static uint8_t gEdit[156];
static mf1::FxParams gFx;
static bool gMono;
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
bool mf1_load_preset(int) { return true; }
bool mf1_import_factory_bank(int, const uint8_t*, size_t) { return true; }
}

static void sundaeChecks() {
  check(iscream_flavor_count() == isc_flavor_count(), "same number of flavors");
  bool same = ISC_FIELDS == IS_FIELDS && ISC_TOPPING_BASE == IS_TOPPING_BASE && ISC_CRUNCH == IS_CRUNCH &&
              ISC_SPRINKLES == IS_SPRINKLES && ISC_TOP(ISC_CARAMEL, 1) == IS_TOP(IS_CARAMEL, 1);
  check(same, "same sundae layout");
  int voiceMismatch = 0, fxMismatch = 0, rangeMismatch = 0, sundaes = 0;
  for (int f = 0; f < isc_flavor_count(); f++) {
    int32_t base[IS_FIELDS];
    iscream_default_sundae(f, base);
    int32_t mine[IS_FIELDS];
    isc_default_sundae(f, mine);
    if (memcmp(base, mine, sizeof base)) voiceMismatch++;
    // Every value of every macro and topping knob, one field at a time.
    for (int field = 0; field < IS_FIELDS; field++) {
      int lo, hi, lo2, hi2;
      iscream_range(field, &lo, &hi);
      isc_range(field, &lo2, &hi2);
      if (lo != lo2 || hi != hi2) rangeMismatch++;
      if (field == IS_FLAVOR) continue;
      for (int v = lo; v <= hi; v++) {
        for (int caramel = 0; caramel < 2; caramel++) {
          int32_t s[IS_FIELDS];
          memcpy(s, base, sizeof s);
          s[field] = v;
          if (caramel) s[IS_TOP(IS_CARAMEL, 0)] = 1;
          uint8_t a[155], b[155];
          iscream_voice(s, a);
          isc_voice(s, b);
          if (memcmp(a, b, 155)) {
            if (voiceMismatch++ < 5)
              for (int i = 0; i < 155; i++)
                if (a[i] != b[i])
                  printf("     flavor %d field %d = %d%s: byte %d is %d, C gives %d\n", f, field, v,
                         caramel ? " with caramel" : "", i, a[i], b[i]);
          }
          iscream_apply(s);
          isc_fx_params_t p;
          isc_fx(s, &p);
          for (int cc = 0; cc < 24; cc++)
            if (mf1_fx_get(cc) != (cc == 3 ? p.cc[3] : isc_fx_params_get(&p, cc))) fxMismatch++;
          for (int pos = 0; pos < 6; pos++)
            if (gFx.order[pos] != p.order[pos]) fxMismatch++;
          sundaes++;
        }
      }
    }
  }
  char what[120];
  snprintf(what, sizeof what, "sundae ranges match IScreamCore (%d differ)", rangeMismatch);
  check(rangeMismatch == 0, what);
  snprintf(what, sizeof what, "%d sundaes give IScreamCore's voice bytes (%d differ)", sundaes, voiceMismatch);
  check(voiceMismatch == 0, what);
  snprintf(what, sizeof what, "and its effect settings and order (%d differ)", fxMismatch);
  check(fxMismatch == 0, what);
}

// ---------------------------------------------------------------- voices

static bool readBanks(const char* dir, std::vector<uint8_t>& vmem) {
  for (int b = 1; b <= 4; b++) {
    char path[512];
    snprintf(path, sizeof path, "%s/FM-1_factory_bank%d.syx", dir, b);
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    uint8_t buf[4104];
    size_t n = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    if (n != sizeof buf) return false;
    vmem.insert(vmem.end(), buf + 6, buf + 6 + 4096);
  }
  return true;
}

// Renders one note through both engines with the patch's LFO and a mod wheel half way, and counts differences.
static int compareVoice(const uint8_t patch[156], int note, int velocity, int blocks, int releaseAt) {
  mf1::Controllers c;
  c.fm1Quirks = true;
  c.pitchBendUp = c.pitchBendDown = 12;
  c.modWheel = 40;
  fm_ctrl_t fc;
  fm_ctrl_init(&fc);
  fc.mod_wheel = 40;
  mf1::Lfo lfo;
  lfo.reset(patch + 137);
  fm_lfo_t flfo;
  memset(&flfo, 0, sizeof flfo);
  fm_lfo_reset(&flfo, patch + 137);
  lfo.keydown();
  fm_lfo_keydown(&flfo);
  mf1::Dx7Voice v;
  v.init(patch, note, velocity, false, c);
  static fm_voice_t fv;
  memset(&fv, 0, sizeof fv);
  fm_voice_init(&fv, patch, note, velocity, 0);
  int diffs = 0;
  for (int b = 0; b < blocks; b++) {
    if (b == releaseAt) {
      v.keyup();
      fm_voice_keyup(&fv);
    }
    int32_t a[mf1::N] = {}, f[FM_N] = {};
    int32_t lv = lfo.getsample(), ld = lfo.getdelay();
    int32_t flv = fm_lfo_sample(&flfo), fld = fm_lfo_delay(&flfo);
    if (lv != flv || ld != fld) diffs++;
    v.compute(a, lv, ld, c);
    fm_voice_compute(&fv, f, flv, fld, &fc);
    for (int i = 0; i < mf1::N; i++)
      if (a[i] != f[i]) diffs++;
    if (v.isFinished() != (fm_voice_finished(&fv) != 0)) diffs++;
  }
  return diffs;
}

static void voiceChecks(const char* banks) {
  std::vector<uint8_t> vmem;
  if (!readBanks(banks, vmem)) {
    check(false, "read the factory banks");
    return;
  }
  int voices = 0, bad = 0;
  for (int p = 0; p < 128; p++) {
    uint8_t patch[156];
    mf1::unpackVmem(&vmem[p * 128], patch);
    const int notes[3] = {36, 60, 84};
    int d = 0;
    for (int n : notes) d += compareVoice(patch, n, 100, 700, 350);
    if (d) {
      bad++;
      printf("     voice %d differs in %d samples\n", p + 1, d);
    }
    voices++;
  }
  char what[120];
  snprintf(what, sizeof what, "%d factory voices render bit for bit as MF-1 (%d differ)", voices, bad);
  check(bad == 0, what);

  // The flavors, as their sundaes ask, through every scoop.
  bad = 0;
  for (int f = 0; f < isc_flavor_count(); f++)
    for (int scoops = 1; scoops <= 3; scoops++) {
      int32_t s[IS_FIELDS];
      isc_default_sundae(f, s);
      s[IS_SCOOPS] = scoops;
      uint8_t patch[156];
      isc_voice(s, patch);
      patch[155] = 0x3f;
      bad += compareVoice(patch, 60, 90, 500, 250) != 0;
    }
  check(bad == 0, "every flavor at every scoop renders bit for bit as MF-1");
}

// ---------------------------------------------------------------- effects

static void fxCheck(const char* name, const std::vector<std::pair<int, int>>& ccs) {
  mf1::FxChain chain;
  chain.prepare(44100);
  mf1::FxParams mp;
  isc_fx_init();
  isc_fx_params_t cp;
  isc_fx_params_default(&cp);
  for (auto [cc, v] : ccs) {
    mp.set(cc, v);
    isc_fx_params_set(&cp, cc, v);
  }
  chain.setParams(mp);
  isc_fx_set_params(&cp);
  // A decaying chord of saws, then silence for the tails.
  double worst = 0, peak = 0;
  const int blocks = 44100 * 3 / 64;
  for (int b = 0; b < blocks; b++) {
    float x[64], y[64];
    for (int i = 0; i < 64; i++) {
      int t = b * 64 + i;
      float s = 0;
      if (t < 44100) {
        float env = expf(-t / 20000.f) * 0.4f;
        const float hz[3] = {220.f, 277.2f, 329.6f};
        for (float h : hz) s += env * (2 * fmodf(t * h / 44100.f, 1.f) - 1) / 3;
      }
      x[i] = y[i] = s;
    }
    chain.process(x, 64);
    isc_fx_process(y, 64);
    for (int i = 0; i < 64; i++) {
      worst = std::max(worst, (double)fabsf(x[i] - y[i]));
      peak = std::max(peak, (double)fabsf(x[i]));
    }
  }
  char what[160];
  // Single against double precision in a few places (oscillator phases, setup maths): allow -60 dB of the peak.
  snprintf(what, sizeof what, "%s matches MF-1 (worst difference %.1f dB below the peak)", name,
           worst > 0 ? 20 * log10(peak / worst) : 999.0);
  check(worst <= peak * 1e-3, what);
}

static void effectChecks() {
  fxCheck("filter low pass", {{0, 1}, {1, 0}, {2, 60}, {3, 7}});
  fxCheck("filter band pass", {{0, 1}, {1, 1}, {2, 80}, {3, 3}});
  fxCheck("filter high pass", {{0, 1}, {1, 2}, {2, 40}, {3, 0}});
  fxCheck("reverb room", {{4, 1}, {5, 0}, {6, 40}, {7, 50}});
  fxCheck("reverb hall", {{4, 1}, {5, 1}, {6, 80}, {7, 60}});
  fxCheck("reverb plate", {{4, 1}, {5, 2}, {6, 60}, {7, 40}});
  fxCheck("delay", {{8, 1}, {9, 60}, {10, 50}, {11, 40}});
  fxCheck("distortion", {{12, 1}, {13, 70}, {14, 40}, {15, 60}});
  fxCheck("chorus", {{16, 1}, {17, 50}, {18, 70}, {19, 60}});
  fxCheck("phaser", {{20, 1}, {21, 40}, {22, 80}, {23, 60}});
  fxCheck("all six", {{0, 1}, {2, 90}, {4, 1}, {8, 1}, {12, 1}, {16, 1}, {20, 1}});
}

int main(int argc, char** argv) {
  mf1::Tables::init(44100);
  sundaeChecks();
  voiceChecks(argc > 1 ? argv[1] : "../../Sources/MF1/Resources/Banks");
  effectChecks();
  printf(failures ? "%d failed\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
