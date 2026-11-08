#include <algorithm>
#include <cstring>

#include "MF1Core.h"
#include "internal.h"

using namespace woof;

namespace {

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// num / den rounded half away from zero, in integers so a C port gives the same voice.
int roundDiv(int num, int den) { return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den); }

// num * t / 100 between lo and hi.
int lerp(int lo, int hi, int t) { return lo + roundDiv((hi - lo) * t, 100); }

// MF1Core's effect numbering, and the order of the chain: the filter first, then the effects as a signal would
// meet them (drive, swirl, chorus, echo, room).
enum { kFilter, kReverb, kDelay, kDistortion, kChorus, kPhaser };
const int kChain[6] = {kFilter, kDistortion, kPhaser, kChorus, kDelay, kReverb};

// The engine's rates for repeats: 1/4, 1/8, 1/8T, 1/16, 1/16T.
const int kStepRates[5] = {2, 4, 5, 6, 7};

void setFx(int cc, int value) {
  if (mf1_fx_get(cc) != value) mf1_fx_set(cc, value);
}

void setEdit(int index, int value) {
  if (mf1_edit_get(index) != value) mf1_edit_set(index, value);
}

// How a place sounds: the reverb's room (0 room, 1 hall, 2 plate) and decay, how much of Distance reaches the
// reverb (percent), the echo's speed (0 is the longest echo, 0.8 s) and how many times it comes back. Outdoors
// there is no reverb tail, only a short reflection off nearby walls or trees, so the outdoor places keep the
// decay short and let only a third of Distance through.
struct Place {
  const char* name;
  int room, decay, wet, echoSpeed, repeats;
};
const Place kPlaces[WF_PLACE_COUNT] = {
    {"Park", 0, 6, 33, 70, 10},       // open grass: a short echo off the trees
    {"Parlour", 0, 20, 100, 85, 5},   // a sitting room, the one place indoors
    {"Rooftops", 0, 10, 33, 14, 50},  // dusk over the chimneys: walls close by, far dogs answer
    {"Farm", 0, 8, 33, 40, 25},       // barns and open fields
    {"River", 0, 6, 33, 28, 25},      // open water
};

const char* const kMoods[7] = {"Mopey", "Sulky", "Sleepy", "Calm", "Happy", "Excited", "Zoomies"};

// The pack: semitones from the key. A pair adds a dog a fifth up, a trio one a fourth down too, and a litter
// puppies above.
const int kPack[WF_PACK_COUNT][5] = {{0, 7}, {0, 7, -5}, {0, 7, 12, 16, 19}};
const int kPackSize[WF_PACK_COUNT] = {2, 3, 5};

// One operator's 21 VCED bytes.
void op(uint8_t* p, int r1, int r2, int r3, int r4, int l1, int l2, int l3, int l4, int level) {
  p[0] = (uint8_t)clampi(r1, 1, 99), p[1] = (uint8_t)clampi(r2, 1, 99);
  p[2] = (uint8_t)clampi(r3, 1, 99), p[3] = (uint8_t)clampi(r4, 1, 99);
  p[4] = (uint8_t)clampi(l1, 0, 99), p[5] = (uint8_t)clampi(l2, 0, 99);
  p[6] = (uint8_t)clampi(l3, 0, 99), p[7] = (uint8_t)clampi(l4, 0, 99);
  p[8] = 39;
  p[15] = 2;  // a harder press barks louder
  p[16] = (uint8_t)clampi(level, 0, 99);
  p[18] = 1;
  p[20] = 7;
}

// A fixed frequency, coarse * 100 + fine as the DX7 writes it.
void fixedAt(uint8_t* p, int f) {
  f = clampi(f, 0, 399);
  p[17] = 1, p[18] = (uint8_t)(f / 100), p[19] = (uint8_t)(f % 100);
}

}  // namespace

int woof_dog_count(void) { return (int)breeds().size(); }

bool woof_dog(int index, WoofDog* out) {
  if (index < 0 || index >= woof_dog_count() || !out) return false;
  const Breed& b = breeds()[index];
  memset(out, 0, sizeof(*out));
  strncpy(out->name, b.voiceName, sizeof(out->name) - 1);
  strncpy(out->breed, b.breed, sizeof(out->breed) - 1);
  strncpy(out->word, b.word, sizeof(out->word) - 1);
  strncpy(out->blurb, b.blurb, sizeof(out->blurb) - 1);
  out->accent = b.accent;
  out->mono = b.mono;
  return true;
}

const char* woof_place_name(int p) { return kPlaces[clampi(p, 0, WF_PLACE_COUNT - 1)].name; }

const char* woof_mood_name(int mood) {
  static const int edges[6] = {16, 31, 46, 56, 71, 86};
  int i = 0;
  while (i < 6 && mood >= edges[i]) i++;
  return kMoods[i];
}

void woof_default_dog(int dog, int32_t out[WF_FIELDS]) {
  dog = clampi(dog, 0, woof_dog_count() - 1);
  memcpy(out, breeds()[dog].day, sizeof(int32_t) * WF_FIELDS);
  out[WF_DOG] = dog;
}

void woof_range(int field, int* lo, int* hi) {
  int a = 0, b = 100;
  switch (field) {
    case WF_DOG: b = woof_dog_count() - 1; break;
    case WF_PLACE: b = WF_PLACE_COUNT - 1; break;
    case WF_PLAY: b = WF_PLAY_COUNT - 1; break;
    case WF_PACK: b = WF_PACK_COUNT - 1; break;
    case WF_TEMPO: a = 40, b = 200; break;
    case WF_STEP: b = 4; break;
  }
  if (lo) *lo = a;
  if (hi) *hi = b;
}

void woof_clamp(int32_t s[WF_FIELDS]) {
  for (int i = 0; i < WF_FIELDS; i++) {
    int lo, hi;
    woof_range(i, &lo, &hi);
    s[i] = clampi(s[i], lo, hi);
  }
}

// The bark a dog's settings ask for. Algorithm 5's three stacks are the throat (OP1 <- OP2, pitched by the key),
// the mouth (OP3 <- OP4: OP3 at a fixed frequency, the formant, with sidebands spaced at the key's pitch) and the
// breath (OP5 <- OP6, a short rough burst, roughened by feedback).
//   Size     moves the pitch by up to an octave and the mouth and breath with it.
//   Gruff    adds feedback and throat brightness; Length stretches the tail; Yelp sets the pitch jump.
//   Mood     sad barks open slowly, darker, and fall; playful ones snap open, brighter, and jump.
//   Howl     sustains the bark, slows its rise and adds vibrato; Growl keeps the throat going, shaken at random.
//   Breath   sets how loud the burst at the start is.
void woof_voice(const int32_t dog[WF_FIELDS], uint8_t v[155]) {
  int32_t s[WF_FIELDS];
  memcpy(s, dog, sizeof(s));
  woof_clamp(s);
  const Breed& b = breeds()[s[WF_DOG]];
  memset(v, 0, 155);

  const int size = s[WF_SIZE] - 50, gruff = s[WF_GRUFF] - 50, length = s[WF_LENGTH] - 50;
  const int yelp = s[WF_YELP] - 50, mood = s[WF_MOOD] - 50;
  const int howl = s[WF_HOWL], growl = s[WF_GROWL], breath = s[WF_BREATH];
  const int shift = -roundDiv(size * 16, 50);
  const int bright = roundDiv(mood * 6, 50);

  // Carriers. A bark swells in over a few tens of milliseconds, holds, then stops short; the mouth opens a
  // little after the throat, the w before the oof.
  const int attack = 72 + (mood > 0 ? roundDiv(mood * 8, 50) : roundDiv(mood * 24, 50)) - roundDiv(howl * 26, 100);
  const int hold = clampi(b.decay - roundDiv(length * 25, 50) - roundDiv(howl * 20, 100), 1, 99);
  const int stop = lerp(62, 40, howl);
  const int held = lerp(86, 96, howl);
  const int sustain = roundDiv(howl * 90, 100);
  const int release = lerp(62, 34, howl);
  uint8_t* throatC = v + 5 * 21;  // VCED stores OP6 first
  uint8_t* throatM = v + 4 * 21;
  uint8_t* mouthC = v + 3 * 21;
  uint8_t* mouthM = v + 2 * 21;
  uint8_t* breathC = v + 1 * 21;
  uint8_t* breathM = v + 0 * 21;
  op(throatC, attack, hold, stop, release, 99, held, std::max(sustain, roundDiv(growl * 82, 100)), 0, 99);
  op(throatM, attack + 4, hold + 4, stop, release, 99, held - 4, std::max(sustain - 10, roundDiv(growl * 70, 100)), 0,
     b.throat + bright + roundDiv(gruff * 10, 50));
  op(mouthC, attack - 6, hold, stop, release, 99, held, sustain, 0, b.mouthLevel);
  fixedAt(mouthC, b.formant + shift);
  op(mouthM, attack, hold + 2, stop, release, 99, held - 3, std::max(0, sustain - 8), 0, b.mouth + bright);
  // The breath: a rough hiss high up, loudest as the bark opens.
  op(breathC, attack + 6, hold + 8, stop, release, 99, held - 8, roundDiv(sustain * 6, 10), 0,
     breath == 0 ? 0 : clampi(b.breath + roundDiv((breath - 50) * 22, 50), 0, 99));
  fixedAt(breathC, b.rasp + shift);
  op(breathM, 99, 99, 99, 99, 99, 99, 99, 0, clampi(76 + roundDiv(gruff * 16, 50), 0, 99));
  fixedAt(breathM, b.rasp + shift - 15);
  for (uint8_t* p : {throatC, mouthC}) p[14] = (uint8_t)(growl == 0 ? 0 : growl < 50 ? 2 : 3);

  // The pitch: a bark starts below the key, jumps up and falls away. A howl rises slowly and higher; a sad dog's
  // bark falls further, a growl sinks under the key.
  const int rise = clampi(b.rise + roundDiv(yelp * 12, 50) + roundDiv(mood * 4, 50) + roundDiv(howl * 6, 100), -10, 40);
  const int fall = b.fall + (mood < 0 ? roundDiv(mood * 6, 50) : 0) - roundDiv(growl * 6, 100);
  uint8_t* peg = v + 126;
  peg[0] = (uint8_t)clampi(90 - roundDiv(howl * 36, 100), 1, 99);
  peg[1] = (uint8_t)clampi(64 - roundDiv(howl * 26, 100), 1, 99);
  peg[2] = 50;
  peg[3] = 50;
  peg[4] = (uint8_t)clampi(50 + rise, 0, 99);
  peg[5] = (uint8_t)clampi(50 + fall, 0, 99);
  peg[6] = (uint8_t)clampi(50 + fall, 0, 99);
  peg[7] = (uint8_t)clampi(50 + b.start, 0, 99);

  v[134] = 4;  // algorithm 5
  v[135] = (uint8_t)clampi(b.gruff + roundDiv(gruff * 4, 50), 0, 7);
  v[136] = 1;  // oscillator key sync: every bark starts the same
  if (growl > 0) {
    // A growl: the throat shaken at random, fast.
    v[137] = (uint8_t)lerp(72, 92, growl);
    v[138] = 0;
    v[139] = (uint8_t)roundDiv(growl * 14, 100);
    v[140] = (uint8_t)roundDiv(growl * 60, 100);
    v[142] = 5;
    v[143] = 3;
  } else {
    // A howl's vibrato, after a moment.
    v[137] = 34;
    v[138] = 45;
    v[139] = (uint8_t)roundDiv(howl * 10, 100);
    v[140] = 0;
    v[142] = 4;  // sine
    v[143] = 3;
  }
  v[141] = 1;  // LFO key sync
  v[144] = (uint8_t)clampi(24 + b.transpose - roundDiv(size * 12, 50), 0, 48);
  size_t n = strlen(b.voiceName);
  for (int i = 0; i < 10; i++) v[145 + i] = (uint8_t)(i < (int)n ? b.voiceName[i] : ' ');
}

int woof_key_notes(const int32_t dog[WF_FIELDS], int note, int out[8]) {
  int32_t s[WF_FIELDS];
  memcpy(s, dog, sizeof(s));
  woof_clamp(s);
  int n = 0;
  auto add = [&](int x) {
    if (x < 0 || x > 127 || n >= 8) return;
    for (int i = 0; i < n; i++)
      if (out[i] == x) return;
    out[n++] = x;
  };
  if (s[WF_PLAY] == WF_CHORUS || s[WF_PLAY] == WF_CHASE) {
    for (int i = 0; i < kPackSize[s[WF_PACK]]; i++) add(note + kPack[s[WF_PACK]][i]);
  } else {
    add(note);
  }
  return n;
}

void woof_apply(const int32_t dog[WF_FIELDS]) {
  int32_t s[WF_FIELDS];
  memcpy(s, dog, sizeof(s));
  woof_clamp(s);
  const Breed& b = breeds()[s[WF_DOG]];

  uint8_t v[155];
  woof_voice(s, v);
  for (int i = 0; i < 145; i++) setEdit(i, v[i]);  // the name stays the breed's
  if (mf1_edit_mono() != b.mono) mf1_edit_set_mono(b.mono);
  if (mf1_env_get(0) != 0) mf1_env_set(0, 0);

  for (int pos = 0; pos < 6; pos++) {
    if (mf1_fx_order(pos) == kChain[pos]) continue;
    for (int other = pos + 1; other < 6; other++)
      if (mf1_fx_order(other) == kChain[pos]) mf1_fx_swap(pos, other);
  }

  // Gruff and growl push the drive; the filter, phaser and chorus stay off, so the bark is the voice's own.
  const int rough = std::max(s[WF_GRUFF] - 50, 0) * 2 + s[WF_GROWL] / 2;
  setFx(0, 0);
  setFx(13, clampi(rough * 6 / 10, 0, 70));
  setFx(14, 45);
  setFx(15, clampi(80 - rough / 6, 60, 80));
  setFx(12, rough > 0 ? 1 : 0);
  setFx(20, 0);
  setFx(16, 0);

  // The place: reverb and echo. Distance and Echo at 0 switch them off.
  const Place& p = kPlaces[s[WF_PLACE]];
  const int space = s[WF_SPACE] - 50;
  setFx(9, clampi(p.repeats + roundDiv(space * 6, 10), 0, 90));
  setFx(10, p.echoSpeed);
  setFx(11, s[WF_ECHO]);
  setFx(8, s[WF_ECHO] > 0 ? 1 : 0);
  setFx(5, p.room);
  setFx(6, clampi(p.decay + roundDiv(space * 8, 10), 0, 100));
  setFx(7, roundDiv(s[WF_DISTANCE] * p.wet, 100));
  setFx(4, s[WF_DISTANCE] > 0 ? 1 : 0);

  // Repeats and chases run on the FM-1's arpeggiator. A repeat is the held keys barking in the order played.
  const bool arp = s[WF_PLAY] == WF_REPEAT || s[WF_PLAY] == WF_CHASE;
  if (arp) {
    const int mode = s[WF_PLAY] == WF_CHASE ? b.arpMode : 5;
    if (mf1_arp_get(MF1_A_MODE) != mode) mf1_arp_set(MF1_A_MODE, mode);
    if (mf1_arp_get(MF1_A_OCTAVES) != 1) mf1_arp_set(MF1_A_OCTAVES, 1);
    if (mf1_arp_get(MF1_A_RATE) != kStepRates[s[WF_STEP]]) mf1_arp_set(MF1_A_RATE, kStepRates[s[WF_STEP]]);
    if (mf1_arp_get(MF1_A_GATE) != 50) mf1_arp_set(MF1_A_GATE, 50);
    if (mf1_arp_get(MF1_A_LATCH) != 0) mf1_arp_set(MF1_A_LATCH, 0);
  }
  if (mf1_global_get(MF1_G_TEMPO) != s[WF_TEMPO]) mf1_global_set(MF1_G_TEMPO, s[WF_TEMPO]);
  if (mf1_arp_on() != arp) mf1_arp_enable(arp);
}

void woof_load(const int32_t dog[WF_FIELDS]) {
  int32_t s[WF_FIELDS];
  memcpy(s, dog, sizeof(s));
  woof_clamp(s);
  mf1_load_preset(s[WF_DOG]);
  woof_apply(s);
}

namespace {

void emptyVoice(uint8_t v[155]) {
  memset(v, 0, 155);
  op(v + 5 * 21, 99, 99, 99, 99, 99, 99, 99, 0, 99);
  for (int k = 0; k < 4; k++) v[126 + k] = 99, v[130 + k] = 50;
  v[134] = 4, v[136] = 1, v[137] = 35, v[143] = 3, v[144] = 24;
  memcpy(v + 145, "INIT DOG  ", 10);
}

// F0 43 0n 09 20 00, 32 voices, checksum, F7.
void bankDump(uint8_t out[4104]) {
  out[0] = 0xF0, out[1] = 0x43, out[2] = 0x00, out[3] = 0x09, out[4] = 0x20, out[5] = 0x00;
  uint8_t empty[155];
  emptyVoice(empty);
  for (int i = 0; i < 32; i++) {
    uint8_t v[155];
    if (i < woof_dog_count()) {
      int32_t d[WF_FIELDS];
      woof_default_dog(i, d);
      woof_voice(d, v);
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

void woof_export_bank(uint8_t out[4104]) { bankDump(out); }

void woof_install(void) {
  uint8_t dump[4104];
  bankDump(dump);
  for (int b = 0; b < 4; b++) mf1_import_factory_bank(b, dump, sizeof(dump));
}
