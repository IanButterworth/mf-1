#include <algorithm>
#include <cstring>

#include "MF1Core.h"
#include "internal.h"

using namespace arpeggi;

namespace {

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// num / den rounded half away from zero, in integers so a C port gives the same voice.
int roundDiv(int num, int den) { return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den); }

// MF1Core's effect numbering, and the order of the chain, as in the other modes.
enum { kFilter, kReverb, kDelay, kDistortion, kChorus, kPhaser };
const int kChain[6] = {kFilter, kDistortion, kPhaser, kChorus, kDelay, kReverb};

const char* const kShapes[AP_SHAPE_COUNT] = {"Up",     "Down", "Bounce",  "Order", "Converge", "Thumb",
                                             "Spiral", "Walk", "Shuffle", "Chord", "Strum"};
// Five and seven to a beat are written as the notes they would be: twentieths and twenty-eighths.
const char* const kRates[AP_RATE_COUNT] = {"1/2",  "1/4",   "1/4.",  "1/8",  "1/8.", "1/8T",
                                           "1/16", "1/16.", "1/16T", "1/20", "1/28", "1/32"};
const char* const kRhythms[AP_RHYTHM_COUNT] = {"Every",   "7 of 8", "5 of 8", "3 of 8", "5 of 12",
                                               "Ratchet", "Gallop", "Skip",   "Fall"};
const char* const kRelations[AP_RELATION_COUNT] = {"Together", "Hocket", "Answer", "Canon", "Mirror", "Trade"};
const char* const kHolds[AP_HOLD_COUNT] = {"Hold", "Latch", "Ring"};
const char* const kSounds[AP_SOUND_COUNT] = {"Pluck", "Glass", "Mallet", "Pad", "Chip", "Bass", "Bell", "Keys"};

void setFx(int cc, int value) {
  if (mf1_fx_get(cc) != value) mf1_fx_set(cc, value);
}

void setEdit(int index, int value) {
  if (mf1_edit_get(index) != value) mf1_edit_set(index, value);
}

// A ratio times 100 as the DX7 writes it: coarse 0 is a half, and fine adds hundredths of the coarse ratio.
void ratioAt(uint8_t* p, int ratio) {
  int coarse = ratio < 100 ? 0 : std::min(31, ratio / 100);
  int fine = coarse == 0 ? roundDiv((ratio - 50) * 100, 50) : roundDiv((ratio - coarse * 100) * 100, coarse * 100);
  p[17] = 0, p[18] = (uint8_t)coarse, p[19] = (uint8_t)clampi(fine, 0, 99);
}

}  // namespace

int arpeggi_scene_count(void) { return (int)scenes().size(); }

bool arpeggi_scene(int index, ApScene* out) {
  if (index < 0 || index >= arpeggi_scene_count() || !out) return false;
  const Scene& c = scenes()[index];
  memset(out, 0, sizeof(*out));
  strncpy(out->name, c.voiceName, sizeof(out->name) - 1);
  strncpy(out->title, c.title, sizeof(out->title) - 1);
  strncpy(out->blurb, c.blurb, sizeof(out->blurb) - 1);
  return true;
}

const char* arpeggi_shape_name(int i) { return kShapes[clampi(i, 0, AP_SHAPE_COUNT - 1)]; }
const char* arpeggi_rate_name(int i) { return kRates[clampi(i, 0, AP_RATE_COUNT - 1)]; }
const char* arpeggi_rhythm_name(int i) { return kRhythms[clampi(i, 0, AP_RHYTHM_COUNT - 1)]; }
const char* arpeggi_relation_name(int i) { return kRelations[clampi(i, 0, AP_RELATION_COUNT - 1)]; }
const char* arpeggi_hold_name(int i) { return kHolds[clampi(i, 0, AP_HOLD_COUNT - 1)]; }
const char* arpeggi_sound_name(int i) { return kSounds[clampi(i, 0, AP_SOUND_COUNT - 1)]; }

void arpeggi_default_scene(int scene, int32_t out[AP_FIELDS]) {
  scene = clampi(scene, 0, arpeggi_scene_count() - 1);
  memcpy(out, scenes()[scene].s, sizeof(int32_t) * AP_FIELDS);
  out[AP_SCENE] = scene;
}

int arpeggi_default_seed(int scene, int8_t out[AP_MAX_POOL]) {
  const Scene& c = scenes()[clampi(scene, 0, arpeggi_scene_count() - 1)];
  int n = std::min<int>(AP_MAX_POOL, (int)c.seed.size());
  for (int i = 0; i < n; i++) out[i] = (int8_t)c.seed[i];
  return n;
}

void arpeggi_range(int field, int* lo, int* hi) {
  int a = 0, b = 100;
  switch (field) {
    case AP_SCENE: b = arpeggi_scene_count() - 1; break;
    case AP_RELATION: b = AP_RELATION_COUNT - 1; break;
    case AP_DUO: b = 1; break;
    case AP_A_SHAPE:
    case AP_B_SHAPE: b = AP_SHAPE_COUNT - 1; break;
    case AP_A_RATE:
    case AP_B_RATE: b = AP_RATE_COUNT - 1; break;
    case AP_A_RHYTHM:
    case AP_B_RHYTHM: b = AP_RHYTHM_COUNT - 1; break;
    case AP_A_GATE:
    case AP_B_GATE: a = 5, b = 200; break;
    case AP_HOLD_MODE: b = AP_HOLD_COUNT - 1; break;
    case AP_RING_SIZE: a = 1, b = 8; break;
    case AP_SPAN: a = 1, b = AP_MAX_SPAN; break;
    case AP_B_PITCH: a = -24, b = 24; break;
    case AP_SOUND: b = AP_SOUND_COUNT - 1; break;
    case AP_TEMPO: a = 40, b = 240; break;
    case AP_ECHO_STEPS: a = 1, b = 8; break;
  }
  if (lo) *lo = a;
  if (hi) *hi = b;
}

void arpeggi_clamp(int32_t s[AP_FIELDS]) {
  for (int i = 0; i < AP_FIELDS; i++) {
    int lo, hi;
    arpeggi_range(i, &lo, &hi);
    s[i] = clampi(s[i], lo, hi);
  }
}

// The sound a scene's settings ask for. Bright moves the marked operators' levels by up to 14 either way; Decay
// slows or speeds every decay and the release, so notes ring longer or stop shorter.
void arpeggi_voice(const int32_t settings[AP_FIELDS], uint8_t v[155]) {
  int32_t s[AP_FIELDS];
  memcpy(s, settings, sizeof(s));
  arpeggi_clamp(s);
  const Sound& snd = sound(s[AP_SOUND]);
  memset(v, 0, 155);
  const int bright = roundDiv((s[AP_BRIGHT] - 50) * 14, 50);
  const int decay = s[AP_DECAY] - 50;
  for (int k = 0; k < 6; k++) {
    const Op& o = snd.op[k];
    uint8_t* p = v + (5 - k) * 21;  // VCED stores OP6 first
    p[0] = (uint8_t)clampi(o.r[0], 1, 99);
    p[1] = (uint8_t)clampi(o.r[1] - roundDiv(decay * 16, 50), 1, 99);
    p[2] = (uint8_t)clampi(o.r[2] - roundDiv(decay * 16, 50), 1, 99);
    p[3] = (uint8_t)clampi(o.r[3] - roundDiv(decay * 12, 50), 1, 99);
    for (int i = 0; i < 4; i++) p[4 + i] = (uint8_t)clampi(o.l[i], 0, 99);
    p[8] = 39;
    p[13] = (uint8_t)clampi(o.rateScale, 0, 7);
    p[14] = (uint8_t)clampi(o.ams, 0, 3);
    p[16] = (uint8_t)clampi(o.level + (o.bright && o.level > 0 ? bright : 0), 0, 99);
    ratioAt(p, o.ratio);
    p[20] = (uint8_t)clampi(o.detune, 0, 14);
  }
  for (int k = 0; k < 4; k++) v[126 + k] = 99, v[130 + k] = 50;
  v[134] = (uint8_t)(snd.algorithm - 1);
  v[135] = (uint8_t)clampi(snd.feedback, 0, 7);
  v[136] = 1;  // oscillator key sync: every strike starts the same
  v[137] = (uint8_t)snd.lfoSpeed;
  v[138] = (uint8_t)snd.lfoDelay;
  v[139] = (uint8_t)snd.pmd;
  v[140] = (uint8_t)snd.amd;
  v[141] = snd.lfoSync ? 1 : 0;
  v[142] = (uint8_t)snd.lfoWave;
  v[143] = (uint8_t)snd.pms;
  v[144] = (uint8_t)clampi(24 + snd.transpose, 0, 48);
  const Scene& scene = scenes()[s[AP_SCENE]];
  size_t n = strlen(scene.voiceName);
  for (int i = 0; i < 10; i++) v[145 + i] = (uint8_t)(i < (int)n ? scene.voiceName[i] : ' ');
}

void arpeggi_apply(const int32_t settings[AP_FIELDS]) {
  int32_t s[AP_FIELDS];
  memcpy(s, settings, sizeof(s));
  arpeggi_clamp(s);
  const Sound& snd = sound(s[AP_SOUND]);

  uint8_t v[155];
  arpeggi_voice(s, v);
  for (int i = 0; i < 145; i++) setEdit(i, v[i]);  // the name stays the scene's
  if (mf1_edit_mono()) mf1_edit_set_mono(false);
  if (mf1_env_get(0) != 0) mf1_env_set(0, 0);
  // The lanes are Arpeggi's own, so the engine's arpeggiator stays off.
  if (mf1_arp_on()) mf1_arp_enable(false);

  for (int pos = 0; pos < 6; pos++) {
    if (mf1_fx_order(pos) == kChain[pos]) continue;
    for (int other = pos + 1; other < 6; other++)
      if (mf1_fx_order(other) == kChain[pos]) mf1_fx_swap(pos, other);
  }
  // No filter, drive or phaser: the sound is the voice's own, with the chorus some sounds ask for.
  setFx(0, 0);
  setFx(12, 0);
  setFx(20, 0);
  setFx(17, 20);
  setFx(18, 55);
  setFx(19, snd.chorus);
  setFx(16, snd.chorus > 0 ? 1 : 0);

  // Room: the reverb's mix, with a longer tail as it grows.
  setFx(5, snd.roomType);
  setFx(6, clampi(24 + s[AP_ROOM] * 3 / 5, 0, 100));
  setFx(7, roundDiv(s[AP_ROOM] * 70, 100));
  setFx(4, s[AP_ROOM] > 0 ? 1 : 0);

  // Echo: the delay, its time a whole number of sixteenths at the tempo, as near as its 0.1 to 0.8 s reach allows.
  // Its feedback grows with the mix, so a wet echo also repeats longer.
  const double step = 15.0 / s[AP_TEMPO];
  int steps = s[AP_ECHO_STEPS];
  while (steps > 1 && steps * step > 0.8) steps--;
  while (steps < 64 && steps * step < 0.1) steps++;
  const double seconds = std::max(0.1, std::min(0.8, steps * step));
  setFx(10, clampi((int)((0.8 - seconds) / 0.7 * 100 + 0.5), 0, 100));
  setFx(9, clampi(20 + s[AP_ECHO] / 2, 0, 70));
  setFx(11, s[AP_ECHO]);
  setFx(8, s[AP_ECHO] > 0 ? 1 : 0);

  if (mf1_global_get(MF1_G_TEMPO) != s[AP_TEMPO]) mf1_global_set(MF1_G_TEMPO, s[AP_TEMPO]);
}

void arpeggi_load(const int32_t settings[AP_FIELDS]) {
  int32_t s[AP_FIELDS];
  memcpy(s, settings, sizeof(s));
  arpeggi_clamp(s);
  mf1_load_preset(s[AP_SCENE]);
  arpeggi_apply(s);
}

namespace {

void emptyVoice(uint8_t v[155]) {
  memset(v, 0, 155);
  uint8_t* c = v + 5 * 21;
  for (int i = 0; i < 4; i++) c[i] = 99, c[4 + i] = 99;
  c[7] = 0, c[16] = 99, c[18] = 1, c[20] = 7;
  for (int k = 0; k < 4; k++) v[126 + k] = 99, v[130 + k] = 50;
  v[134] = 4, v[136] = 1, v[137] = 35, v[143] = 3, v[144] = 24;
  memcpy(v + 145, "INIT ARP  ", 10);
}

// F0 43 0n 09 20 00, 32 voices, checksum, F7.
void bankDump(uint8_t out[4104]) {
  out[0] = 0xF0, out[1] = 0x43, out[2] = 0x00, out[3] = 0x09, out[4] = 0x20, out[5] = 0x00;
  uint8_t empty[155];
  emptyVoice(empty);
  for (int i = 0; i < 32; i++) {
    uint8_t v[155];
    if (i < arpeggi_scene_count()) {
      int32_t d[AP_FIELDS];
      arpeggi_default_scene(i, d);
      arpeggi_voice(d, v);
    } else {
      memcpy(v, empty, sizeof(v));
    }
    packVoice(v, out + 6 + i * 128);
  }
  int sum = 0;
  for (int i = 0; i < 4096; i++) sum += out[6 + i];
  out[6 + 4096] = (uint8_t)((128 - (sum & 0x7f)) & 0x7f);
  out[6 + 4097] = 0xF7;
}

}  // namespace

void arpeggi_export_bank(uint8_t out[4104]) { bankDump(out); }

void arpeggi_install(void) {
  uint8_t dump[4104];
  bankDump(dump);
  for (int b = 0; b < 4; b++) mf1_import_factory_bank(b, dump, sizeof(dump));
}
