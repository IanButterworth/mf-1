#include <algorithm>
#include <cstring>

#include "MF1Core.h"
#include "internal.h"

using namespace almanac;

namespace {

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// num / den rounded half away from zero, in integers so a C port gives the same voice.
int roundDiv(int num, int den) { return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den); }

// num * t / 100 between lo and hi.
int lerp(int lo, int hi, int t) { return lo + roundDiv((hi - lo) * t, 100); }

// MF1Core's effect numbering, and the order of the chain: the filter first, then the effects as a
// signal would meet them (drive, swirl, chorus, echo, room).
enum { kFilter, kReverb, kDelay, kDistortion, kChorus, kPhaser };
const int kChain[6] = {kFilter, kDistortion, kPhaser, kChorus, kDelay, kReverb};

// The engine's rates for a pattern's notes: 1/4, 1/8, 1/8T, 1/16, 1/16T.
const int kStepRates[5] = {2, 4, 5, 6, 7};

void setFx(int cc, int value) {
  if (mf1_fx_get(cc) != value) mf1_fx_set(cc, value);
}

void setEdit(int index, int value) {
  if (mf1_edit_get(index) != value) mf1_edit_set(index, value);
}

// How a place sounds: the reverb's room (0 room, 1 hall, 2 plate) and decay, the echo's speed (0 is
// the longest echo, 0.8 s) and how many times it comes back.
struct Place {
  const char* name;
  int room, decay, echoSpeed, repeats;
};
const Place kPlaces[AL_PLACE_COUNT] = {
    {"Meadow", 0, 35, 70, 15},     // open air: a little room, a short echo that soon goes
    {"Forest", 0, 55, 88, 45},     // close trunks: quick echoes that cluster
    {"Mountains", 1, 72, 12, 45},  // a long echo back off the far side
    {"Coast", 1, 60, 40, 30},      // wide and slow
    {"Lake", 2, 76, 30, 20},       // a bright, long ring over still water
    {"Village", 2, 40, 62, 10},    // walls close by: a slap back
};

const char* const kMoods[7] = {"Gloomy", "Wistful", "Pensive", "Calm", "Content", "Glad", "Elated"};

}  // namespace

int almanac_month_count(void) { return (int)months().size(); }

bool almanac_month(int index, AlmanacMonth* out) {
  if (index < 0 || index >= almanac_month_count() || !out) return false;
  const Month& m = months()[index];
  memset(out, 0, sizeof(*out));
  strncpy(out->name, m.voiceName, sizeof(out->name) - 1);
  strncpy(out->month, m.month, sizeof(out->month) - 1);
  strncpy(out->title, m.title, sizeof(out->title) - 1);
  strncpy(out->blurb, m.blurb, sizeof(out->blurb) - 1);
  out->skyTop = m.skyTop;
  out->skyBottom = m.skyBottom;
  out->ground = m.ground;
  out->far = m.far;
  out->accent = m.accent;
  out->season = m.season;
  out->mono = m.mono;
  return true;
}

const char* almanac_sky_name(int s) { return sky(s).name; }
const char* almanac_place_name(int p) { return kPlaces[clampi(p, 0, AL_PLACE_COUNT - 1)].name; }

const char* almanac_mood_name(int mood) {
  static const int edges[6] = {16, 31, 46, 56, 71, 86};
  int i = 0;
  while (i < 6 && mood >= edges[i]) i++;
  return kMoods[i];
}

void almanac_default_day(int month, int32_t out[AL_FIELDS]) {
  month = clampi(month, 0, almanac_month_count() - 1);
  memcpy(out, months()[month].day, sizeof(int32_t) * AL_FIELDS);
}

void almanac_range(int field, int* lo, int* hi) {
  int a = 0, b = 100;
  switch (field) {
    case AL_MONTH: b = almanac_month_count() - 1; break;
    case AL_SKY: b = AL_SKY_COUNT - 1; break;
    case AL_PLACE: b = AL_PLACE_COUNT - 1; break;
    case AL_PLAY: b = AL_PLAY_COUNT - 1; break;
    case AL_VOICING: b = AL_VOICING_COUNT - 1; break;
    case AL_TEMPO: a = 40, b = 200; break;
    case AL_STEP: b = 4; break;
  }
  if (lo) *lo = a;
  if (hi) *hi = b;
}

void almanac_clamp(int32_t s[AL_FIELDS]) {
  for (int i = 0; i < AL_FIELDS; i++) {
    int lo, hi;
    almanac_range(i, &lo, &hi);
    s[i] = clampi(s[i], lo, hi);
  }
}

// The voice a day asks for: the month as composed under the day's sky, then five macros, each of
// them ordinary DX7 parameters.
//   Mood     wistful opens the notes slowly, darkens them and lets the two stacks drift apart;
//            glad opens them quickly and brightens them.
//   Warmth   raises or lowers the modulators.
//   Linger   slows or speeds the releases (and the decays of the months that fall away).
//   Shimmer  sets the vibrato depth.
//   Elements sets how loud the weather's own stack is, and Gusts how fast the LFO moves it.
void almanac_voice(const int32_t day[AL_FIELDS], uint8_t v[155]) {
  int32_t s[AL_FIELDS];
  memcpy(s, day, sizeof(s));
  almanac_clamp(s);
  const Month& m = months()[s[AL_MONTH]];
  const Sky& k = sky(s[AL_SKY]);
  baseVoice(m, k, v);

  const int mood = s[AL_MOOD] - 50;
  const int warmth = s[AL_WARMTH] < 50 ? roundDiv((s[AL_WARMTH] - 50) * 6, 10) : roundDiv((s[AL_WARMTH] - 50) * 24, 100);
  const int bright = warmth + roundDiv(mood * 12, 100);
  const int attack = roundDiv(mood * 4, 10);
  const int drift = mood < 0 ? roundDiv(-mood * 4, 50) : 0;
  const int linger = roundDiv((s[AL_LINGER] - 50) * 5, 10);

  for (int i = 0; i < 4; i++) {
    const Op& o = m.op[i];
    uint8_t* p = v + (5 - i) * 21;
    if (o.mod) {
      p[16] = (uint8_t)clampi(o.ol + bright, 0, 99);
    } else {
      p[0] = (uint8_t)clampi(o.r[0] + attack, 1, 99);
    }
    p[20] = (uint8_t)clampi(p[20] + (i < 2 ? -drift : drift), 0, 14);
    p[3] = (uint8_t)clampi(o.r[3] - linger, 1, 99);
    if (m.decays) {
      p[1] = (uint8_t)clampi(o.r[1] - linger, 1, 99);
      p[2] = (uint8_t)clampi(o.r[2] - linger, 1, 99);
    }
  }
  // The weather's stack: silent at Elements 0, then from quiet to loud. Its release follows Linger too.
  uint8_t* w = v + 1 * 21;
  w[16] = (uint8_t)(s[AL_ELEMENTS] == 0 || k.loud == 0 ? 0 : lerp(k.quiet, k.loud, s[AL_ELEMENTS]));
  w[3] = (uint8_t)clampi(k.carrier.r[3] - linger, 1, 99);
  v[137] = (uint8_t)lerp(k.lfoSlow, k.lfoFast, s[AL_GUSTS]);
  v[139] = (uint8_t)clampi(roundDiv(s[AL_SHIMMER] * 35, 100), 0, 99);
}

int almanac_key_notes(const int32_t day[AL_FIELDS], int note, bool left, int out[8]) {
  int32_t s[AL_FIELDS];
  memcpy(s, day, sizeof(s));
  almanac_clamp(s);
  const Month& m = months()[s[AL_MONTH]];
  const int voicing = s[AL_VOICING];
  int n = 0;
  auto add = [&](int x) {
    if (x < 0 || x > 127 || n >= 8) return;
    for (int i = 0; i < n; i++)
      if (out[i] == x) return;
    out[n++] = x;
  };
  switch (s[AL_PLAY]) {
    case AL_SPLIT:
      if (!left) {
        add(note);
        break;
      }
      // The left hand holds a drone: the root an octave down, then its fifth, then the octave below.
      add(note - 12);
      if (voicing >= AL_OPEN) add(note - 5);
      if (voicing == AL_WIDE) add(note - 24);
      break;
    case AL_CHORD:
    case AL_PATTERN:
      if (voicing == AL_WIDE) add(note - 12);
      for (int i = 0; i < m.chordSize; i++) {
        // Open voicing lifts the second note of the chord by an octave.
        add(note + m.chord[i] + (voicing != AL_CLOSE && i == 1 ? 12 : 0));
      }
      break;
    default:
      add(note);
  }
  return n;
}

void almanac_apply(const int32_t day[AL_FIELDS]) {
  int32_t s[AL_FIELDS];
  memcpy(s, day, sizeof(s));
  almanac_clamp(s);
  const Month& m = months()[s[AL_MONTH]];

  uint8_t v[155];
  almanac_voice(s, v);
  for (int i = 0; i < 145; i++) setEdit(i, v[i]);  // the name stays the month's
  if (mf1_edit_mono() != m.mono) mf1_edit_set_mono(m.mono);
  if (mf1_env_get(0) != 0) mf1_env_set(0, 0);

  for (int pos = 0; pos < 6; pos++) {
    if (mf1_fx_order(pos) == kChain[pos]) continue;
    for (int other = pos + 1; other < 6; other++)
      if (mf1_fx_order(other) == kChain[pos]) mf1_fx_swap(pos, other);
  }

  // The sky: filter, drive, phaser and chorus, all scaled by Weather.
  const int sk = s[AL_SKY], w = s[AL_WEATHER], g = s[AL_GUSTS];
  const int swirl = lerp(8, 70, g);  // chorus and phaser rate
  int filterOn = 1, filterType = 0, cutoff = 107, res = 10;
  int chorusOn = 0, chorusMix = 0, chorusDepth = 50;
  int phaserOn = 0, phaserMix = 0, phaserDepth = 50;
  int driveOn = 0, driveGain = 0, driveTone = 40, driveLevel = 70;
  switch (sk) {
    case AL_CLEAR:
      filterOn = 0;
      break;
    case AL_BREEZE:
      filterOn = 0;
      chorusOn = 1, chorusMix = lerp(15, 65, w), chorusDepth = lerp(30, 70, w);
      break;
    case AL_RAIN:
      cutoff = lerp(104, 70, w), res = 10;
      chorusOn = 1, chorusMix = lerp(10, 35, w), chorusDepth = 40;
      break;
    case AL_FOG:
      cutoff = lerp(88, 32, w), res = 5;
      chorusOn = 1, chorusMix = lerp(15, 40, w), chorusDepth = 70;
      break;
    case AL_STORM:
      cutoff = lerp(104, 80, w), res = 15;
      driveOn = 1, driveGain = lerp(10, 70, w), driveLevel = lerp(82, 66, w);
      phaserOn = 1, phaserMix = lerp(25, 70, w), phaserDepth = 75;
      break;
    case AL_SNOW:
      filterType = 2, cutoff = lerp(0, 30, w), res = 15;
      phaserOn = 1, phaserMix = lerp(15, 45, w), phaserDepth = 40;
      break;
  }
  setFx(1, filterType);
  setFx(2, cutoff);
  setFx(3, res);
  setFx(0, filterOn);
  setFx(13, driveGain);
  setFx(14, driveTone);
  setFx(15, driveLevel);
  setFx(12, driveOn);
  setFx(21, swirl);
  setFx(22, phaserDepth);
  setFx(23, phaserMix);
  setFx(20, phaserOn);
  setFx(17, swirl);
  setFx(18, chorusDepth);
  setFx(19, chorusMix);
  setFx(16, chorusOn);

  // The place: reverb and echo. Distance and Echo at 0 switch them off.
  const Place& p = kPlaces[s[AL_PLACE]];
  const int space = s[AL_SPACE] - 50;
  setFx(9, clampi(p.repeats + roundDiv(space * 6, 10), 0, 90));
  setFx(10, p.echoSpeed);
  setFx(11, s[AL_ECHO]);
  setFx(8, s[AL_ECHO] > 0 ? 1 : 0);
  setFx(5, p.room);
  setFx(6, clampi(p.decay + roundDiv(space * 8, 10), 0, 100));
  setFx(7, s[AL_DISTANCE]);
  setFx(4, s[AL_DISTANCE] > 0 ? 1 : 0);

  // A pattern plays the chord on the FM-1's arpeggiator. Other styles leave it off.
  const bool pattern = s[AL_PLAY] == AL_PATTERN;
  if (pattern) {
    if (mf1_arp_get(MF1_A_MODE) != m.arpMode) mf1_arp_set(MF1_A_MODE, m.arpMode);
    const int octaves = m.arpOctaves + (s[AL_VOICING] == AL_WIDE ? 1 : 0);
    if (mf1_arp_get(MF1_A_OCTAVES) != octaves) mf1_arp_set(MF1_A_OCTAVES, octaves);
    if (mf1_arp_get(MF1_A_RATE) != kStepRates[s[AL_STEP]]) mf1_arp_set(MF1_A_RATE, kStepRates[s[AL_STEP]]);
    if (mf1_arp_get(MF1_A_GATE) != 80) mf1_arp_set(MF1_A_GATE, 80);
    if (mf1_arp_get(MF1_A_LATCH) != 0) mf1_arp_set(MF1_A_LATCH, 0);
  }
  if (mf1_global_get(MF1_G_TEMPO) != s[AL_TEMPO]) mf1_global_set(MF1_G_TEMPO, s[AL_TEMPO]);
  if (mf1_arp_on() != pattern) mf1_arp_enable(pattern);
}

void almanac_load(const int32_t day[AL_FIELDS]) {
  int32_t s[AL_FIELDS];
  memcpy(s, day, sizeof(s));
  almanac_clamp(s);
  mf1_load_preset(s[AL_MONTH]);
  almanac_apply(s);
}

namespace {

void emptyVoice(uint8_t v[155]) {
  Month m{"INIT DAY", "", "", "", 0, 0, 0, 0, 0, 0};
  m.op[0] = car(99);
  baseVoice(m, sky(AL_CLEAR), v);
}

// F0 43 0n 09 20 00, 32 voices, checksum, F7.
void bankDump(uint8_t out[4104]) {
  out[0] = 0xF0, out[1] = 0x43, out[2] = 0x00, out[3] = 0x09, out[4] = 0x20, out[5] = 0x00;
  const auto& all = months();
  uint8_t empty[155];
  emptyVoice(empty);
  for (int i = 0; i < 32; i++) {
    uint8_t v[155];
    if (i < (int)all.size())
      almanac_voice(all[i].day, v);
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

void almanac_export_bank(uint8_t out[4104]) { bankDump(out); }

void almanac_install(void) {
  uint8_t dump[4104];
  bankDump(dump);
  for (int b = 0; b < 4; b++) mf1_import_factory_bank(b, dump, sizeof(dump));
}
