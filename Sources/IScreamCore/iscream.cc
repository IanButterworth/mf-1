#include <algorithm>
#include <cstring>

#include "MF1Core.h"
#include "internal.h"

using namespace iscream;

namespace {

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// num / den rounded half away from zero, in integers so the firmware's C gives the same voice.
int roundDiv(int num, int den) { return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den); }

// MF1Core's effect numbering, and the order the toppings are stacked in: the cone first, then
// the effects as a signal would meet them (drive, swirl, chorus, echo, room).
enum { kFilter, kReverb, kDelay, kDistortion, kChorus, kPhaser };
const int kChain[6] = {kFilter, kDistortion, kPhaser, kChorus, kDelay, kReverb};

void setFx(int cc, int value) {
  if (mf1_fx_get(cc) != value) mf1_fx_set(cc, value);
}

void setEdit(int index, int value) {
  if (mf1_edit_get(index) != value) mf1_edit_set(index, value);
}

}  // namespace

int iscream_flavor_count(void) { return (int)flavors().size(); }

bool iscream_flavor(int index, IScreamFlavor* out) {
  if (index < 0 || index >= iscream_flavor_count() || !out) return false;
  const Flavor& f = flavors()[index];
  memset(out, 0, sizeof(*out));
  strncpy(out->name, f.voiceName, sizeof(out->name) - 1);
  strncpy(out->title, f.title, sizeof(out->title) - 1);
  strncpy(out->blurb, f.blurb, sizeof(out->blurb) - 1);
  out->color = f.color;
  out->accent = f.accent;
  out->bits = f.bits;
  out->pattern = f.pattern;
  out->mono = f.mono;
  return true;
}

void iscream_default_sundae(int flavor, int32_t out[IS_FIELDS]) {
  flavor = clampi(flavor, 0, iscream_flavor_count() - 1);
  memcpy(out, flavors()[flavor].sundae, sizeof(int32_t) * IS_FIELDS);
}

void iscream_range(int field, int* lo, int* hi) {
  int a = 0, b = 100;
  switch (field) {
    case IS_FLAVOR: b = iscream_flavor_count() - 1; break;
    case IS_SCOOPS: a = 1, b = 3; break;
    case IS_CONE_ON: b = 1; break;
    case IS_CONE_STYLE: b = 2; break;
    case IS_TOAST: b = 107; break;
    default:
      if (field >= IS_TOPPING_BASE) {
        int topping = (field - IS_TOPPING_BASE) / 4, knob = (field - IS_TOPPING_BASE) % 4;
        if (knob == 0) b = 1;
        if (knob == 3 && topping == IS_SPRINKLES) b = 2;
      }
  }
  if (lo) *lo = a;
  if (hi) *hi = b;
}

void iscream_clamp(int32_t s[IS_FIELDS]) {
  for (int i = 0; i < IS_FIELDS; i++) {
    int lo, hi;
    iscream_range(i, &lo, &hi);
    s[i] = clampi(s[i], lo, hi);
  }
}

// The voice a sundae asks for. Everything comes from the flavor as designed plus four macros, each
// of them ordinary DX7 parameters:
//   Scoops  detunes the three stacks against each other.
//   Sweet   raises or lowers the modulators.
//   Melt    slows or speeds the releases (and the decays of the flavors that fall away by themselves).
//   Fizz    sets the vibrato depth.
// Caramel adds a slightly flat tuning and a little flutter, like a worn tape.
void iscream_voice(const int32_t sundae[IS_FIELDS], uint8_t v[155]) {
  int32_t s[IS_FIELDS];
  memcpy(s, sundae, sizeof(s));
  iscream_clamp(s);
  const Flavor& f = flavors()[s[IS_FLAVOR]];
  baseVoice(f, v);

  const int spread = f.baseSpread + scoopSpread(s[IS_SCOOPS]);
  const bool caramel = s[IS_TOP(IS_CARAMEL, 0)] != 0;
  const int caramelAmount = caramel ? s[IS_TOP(IS_CARAMEL, 1)] : 0;
  const int flat = roundDiv(caramelAmount * 3, 100);
  const int sweetOffset =
      s[IS_SWEET] < 50 ? roundDiv((s[IS_SWEET] - 50) * 6, 10) : roundDiv((s[IS_SWEET] - 50) * 24, 100);
  const int melt = roundDiv((s[IS_MELT] - 50) * 5, 10);

  for (int i = 0; i < 6; i++) {
    const Op& o = f.op[i];
    uint8_t* p = v + (5 - i) * 21;
    p[20] = (uint8_t)clampi(o.det + o.spread * spread - flat, 0, 14);
    if (o.mod) p[16] = (uint8_t)clampi(o.ol + sweetOffset, 0, 99);
    p[3] = (uint8_t)clampi(o.r[3] - melt, 1, 99);
    if (f.decays) {
      p[1] = (uint8_t)clampi(o.r[1] - melt, 1, 99);
      p[2] = (uint8_t)clampi(o.r[2] - melt, 1, 99);
    }
  }
  v[139] = (uint8_t)clampi(roundDiv(s[IS_FIZZ] * 35 + caramelAmount * 12, 100), 0, 99);
}

void iscream_apply(const int32_t sundae[IS_FIELDS]) {
  int32_t s[IS_FIELDS];
  memcpy(s, sundae, sizeof(s));
  iscream_clamp(s);
  const Flavor& f = flavors()[s[IS_FLAVOR]];

  uint8_t v[155];
  iscream_voice(s, v);
  for (int i = 0; i < 145; i++) setEdit(i, v[i]);  // the name stays the flavor's
  if (mf1_edit_mono() != f.mono) mf1_edit_set_mono(f.mono);
  if (mf1_env_get(0) != 0) mf1_env_set(0, 0);

  // The chain order is part of the sound, so every sundae sets it.
  for (int pos = 0; pos < 6; pos++) {
    if (mf1_fx_order(pos) == kChain[pos]) continue;
    for (int other = pos + 1; other < 6; other++)
      if (mf1_fx_order(other) == kChain[pos]) mf1_fx_swap(pos, other);
  }

  setFx(1, s[IS_CONE_STYLE]);
  setFx(2, s[IS_TOAST]);
  setFx(3, s[IS_CRUNCH]);
  setFx(0, s[IS_CONE_ON]);

  auto top = [&](int t, int knob) { return s[IS_TOP(t, knob)]; };
  // Whipped cream: chorus.
  setFx(17, top(IS_WHIPPED_CREAM, 2));
  setFx(18, top(IS_WHIPPED_CREAM, 3));
  setFx(19, top(IS_WHIPPED_CREAM, 1));
  setFx(16, top(IS_WHIPPED_CREAM, 0));
  // Hot fudge: distortion.
  setFx(13, top(IS_HOT_FUDGE, 1));
  setFx(14, top(IS_HOT_FUDGE, 2));
  setFx(15, top(IS_HOT_FUDGE, 3));
  setFx(12, top(IS_HOT_FUDGE, 0));
  // Caramel: phaser.
  setFx(21, top(IS_CARAMEL, 2));
  setFx(22, top(IS_CARAMEL, 3));
  setFx(23, top(IS_CARAMEL, 1));
  setFx(20, top(IS_CARAMEL, 0));
  // Chocolate chips: delay.
  setFx(9, top(IS_CHOC_CHIPS, 2));
  setFx(10, top(IS_CHOC_CHIPS, 3));
  setFx(11, top(IS_CHOC_CHIPS, 1));
  setFx(8, top(IS_CHOC_CHIPS, 0));
  // Rainbow sprinkles: reverb.
  setFx(5, top(IS_SPRINKLES, 3));
  setFx(6, top(IS_SPRINKLES, 2));
  setFx(7, top(IS_SPRINKLES, 1));
  setFx(4, top(IS_SPRINKLES, 0));
}

void iscream_load(const int32_t sundae[IS_FIELDS]) {
  int32_t s[IS_FIELDS];
  memcpy(s, sundae, sizeof(s));
  iscream_clamp(s);
  mf1_load_preset(s[IS_FLAVOR]);
  iscream_apply(s);
}

namespace {

void emptyVoice(uint8_t v[155]) {
  Flavor f{"INIT SCOOP", "", "", 0, 0};
  f.op[0] = car(99);
  baseVoice(f, v);
}

// F0 43 0n 09 20 00, 32 voices, checksum, F7.
void bankDump(uint8_t out[4104]) {
  out[0] = 0xF0, out[1] = 0x43, out[2] = 0x00, out[3] = 0x09, out[4] = 0x20, out[5] = 0x00;
  const auto& all = flavors();
  uint8_t empty[155];
  emptyVoice(empty);
  for (int i = 0; i < 32; i++) {
    uint8_t v[155];
    if (i < (int)all.size())
      baseVoice(all[i], v);
    else
      memcpy(v, empty, sizeof(v));
    packVoice(v, out + 6 + i * 128);
  }
  int sum = 0;
  for (int i = 0; i < 4096; i++) sum += out[6 + i];
  out[6 + 4096] = (uint8_t)((128 - (sum & 0x7f)) & 0x7f);
  out[6 + 4097] = 0xF7;
}

}  // namespace

void iscream_export_bank(uint8_t out[4104]) { bankDump(out); }

void iscream_install(void) {
  uint8_t dump[4104];
  bankDump(dump);
  for (int b = 0; b < 4; b++) mf1_import_factory_bank(b, dump, sizeof(dump));
}
