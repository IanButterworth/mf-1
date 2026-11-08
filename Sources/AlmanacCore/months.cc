#include <algorithm>
#include <cstring>

#include "internal.h"

namespace almanac {

namespace {

struct Day {
  int sky, weather, elements, gusts;
  int place, distance, echo;
  int mood, shimmer = 0;
  int tempo = 96, step = 1;
};

void compose(Month& m, Day d) {
  int32_t* s = m.day;
  s[AL_MONTH] = 0;
  s[AL_MOOD] = d.mood;
  s[AL_WARMTH] = 50;
  s[AL_LINGER] = 50;
  s[AL_SHIMMER] = d.shimmer;
  s[AL_SKY] = d.sky;
  s[AL_WEATHER] = d.weather;
  s[AL_ELEMENTS] = d.elements;
  s[AL_GUSTS] = d.gusts;
  s[AL_PLACE] = d.place;
  s[AL_DISTANCE] = d.distance;
  s[AL_ECHO] = d.echo;
  s[AL_SPACE] = 50;
  s[AL_PLAY] = AL_SINGLE;
  s[AL_VOICING] = AL_CLOSE;
  s[AL_TEMPO] = d.tempo;
  s[AL_STEP] = d.step;
}

// The months. Each voice is two stacks of two operators; modulator levels are written so that
// Warmth at 50 is as composed, and carriers are balanced for about equal loudness across months.
std::vector<Month> build() {
  std::vector<Month> all;

  {  // January: still, frozen air over a lake. Glassy bells that ring and fade.
    Month m{"FROST", "January", "Frost", "Glass bells over the ice",
            0x9FC3E6, 0xE6F0F8, 0xF4F8FC, 0x7F97B8, 0x2F5C8A, 0};
    m.decays = true;
    m.op[0] = car(96).env(99, 35, 25, 40, 99, 70, 0, 0).vel(3);
    m.op[1] = mod(72).ratio(2, 75).env(99, 45, 30, 40, 99, 60, 0, 0).vel(3);
    m.op[2] = car(84).ratio(4).env(99, 50, 30, 40, 99, 55, 0, 0).vel(3);
    m.op[3] = mod(64).ratio(7).env(99, 55, 35, 40, 99, 40, 0, 0).vel(3);
    m.chordOf({0, 7, 14}).pattern(4, 2);  // open fifths; icicles dripping at random
    compose(m, {AL_SNOW, 45, 35, 25, AL_LAKE, 40, 20, 40, 0, 72, 3});
    all.push_back(m);
  }
  {  // February: grey thaw, waiting for the light. A soft, hollow ocarina.
    Month m{"THAW", "February", "Thaw", "Hollow breath, waiting",
            0xA9B4C2, 0xDCE1E6, 0xCFD6D2, 0x8E9AA6, 0x4A5866, 0};
    m.op[0] = car(97).env(55, 40, 30, 45, 99, 95, 92, 0).vel(2);
    m.op[1] = mod(66).env(50, 35, 30, 45, 99, 90, 85, 0).vel(2);
    m.op[2] = car(72).ratio(2).env(45, 40, 30, 45, 99, 90, 85, 0).vel(2);
    m.op[3] = mod(52).env(60, 40, 30, 45, 99, 85, 80, 0).vel(2);
    m.chordOf({0, 3, 10, 14}).pattern(1, 1);  // minor ninth, falling
    compose(m, {AL_FOG, 50, 30, 20, AL_FOREST, 45, 15, 30, 15, 66, 1});
    all.push_back(m);
  }
  {  // March: wind on the hills. A reedy lead that rises into each note.
    Month m{"GALE", "March", "Gale", "Reedy lead, rising",
            0x7FB2E5, 0xD4E8F7, 0x8DB36A, 0x6E8FA8, 0x2E6B3F, 1};
    m.spread = 2;
    m.peg[0] = 60, m.peg[4] = 46;  // starts a little flat and rises
    m.op[0] = car(96).env(80, 50, 40, 50, 99, 95, 92, 0).vel(2);
    m.op[1] = mod(82).ratio(2).env(75, 45, 35, 50, 99, 92, 88, 0).vel(2);
    m.op[2] = car(88).env(80, 50, 40, 50, 99, 95, 92, 0).vel(2);
    m.op[3] = mod(76).ratio(3).env(70, 45, 35, 50, 99, 90, 85, 0).vel(2);
    m.chordOf({0, 5, 7, 12}).pattern(2, 2);  // suspended, never quite settling
    compose(m, {AL_BREEZE, 60, 55, 60, AL_MOUNTAINS, 35, 30, 55, 20, 112, 3});
    all.push_back(m);
  }
  {  // April: showers in a wood. A kalimba plucked between the drops.
    Month m{"SHOWERS", "April", "Showers", "Kalimba between the drops",
            0x8DB8D8, 0xD8EAF2, 0x6FB861, 0x7C9DB5, 0x2F7A4A, 1};
    m.decays = true;
    m.op[0] = car(98).env(99, 55, 35, 45, 99, 70, 0, 0).vel(3);
    m.op[1] = mod(70).ratio(3).env(99, 70, 50, 50, 99, 40, 0, 0).vel(3);
    m.op[2] = car(78).ratio(5).env(99, 75, 50, 50, 99, 30, 0, 0).vel(3);
    m.op[3] = mod(55).env(99, 70, 50, 50, 99, 30, 0, 0).vel(3);
    m.chordOf({0, 4, 7, 14}).pattern(4, 2);  // add nine, in drops
    compose(m, {AL_RAIN, 40, 45, 55, AL_FOREST, 30, 35, 65, 0, 100, 3});
    all.push_back(m);
  }
  {  // May: blossom in a meadow. A flute with a breathy chiff.
    Month m{"BLOSSOM", "May", "Blossom", "Flute in the meadow",
            0x6EC1F0, 0xE3F5FF, 0x7CCB5B, 0x93C7A0, 0xC2477E, 1};
    m.lfoDelay = 50;
    m.op[0] = car(97).env(65, 45, 40, 50, 99, 96, 94, 0).vel(2);
    m.op[1] = mod(68).env(70, 40, 40, 50, 99, 85, 80, 0).vel(2);
    m.op[2] = car(70).ratio(2).env(99, 60, 50, 50, 99, 40, 30, 0).vel(3);
    m.op[3] = mod(78).ratio(7).env(99, 70, 60, 50, 99, 30, 0, 0).vel(3);
    m.chordOf({0, 4, 7, 9, 14}).pattern(0, 2);  // six-nine, climbing
    compose(m, {AL_CLEAR, 30, 0, 30, AL_MEADOW, 35, 25, 85, 30, 120, 3});
    all.push_back(m);
  }
  {  // June: the longest day. Warm strings that open slowly.
    Month m{"MIDSUMMER", "June", "Midsummer", "Warm strings, long light",
            0x4FB0F0, 0xFFF4D6, 0x5DB84A, 0x7DBF8F, 0xE07B00, 2};
    m.spread = 3;
    m.op[0] = car(95).env(45, 40, 35, 40, 99, 96, 94, 0).vel(2);
    m.op[1] = mod(82).env(50, 40, 35, 40, 99, 95, 92, 0).vel(2);
    m.op[2] = car(95).env(45, 40, 35, 40, 99, 96, 94, 0).vel(2);
    m.op[3] = mod(82).env(50, 40, 35, 40, 99, 95, 92, 0).vel(2);
    m.chordOf({0, 4, 7, 11, 14}).pattern(2, 1);  // major ninth, rocking
    compose(m, {AL_CLEAR, 30, 0, 25, AL_MEADOW, 45, 15, 90, 10, 84, 1});
    all.push_back(m);
  }
  {  // July: heat haze on the coast. A lazy tine piano.
    Month m{"HEAT HAZE", "July", "Heat Haze", "Lazy tine piano by the sea",
            0x63B7EB, 0xFFE3B0, 0xC8B85A, 0x9CC0C9, 0xD9480F, 2};
    m.decays = true;
    m.op[0] = car(97).env(99, 40, 25, 45, 99, 75, 60, 0).vel(3);
    m.op[1] = mod(66).env(99, 45, 30, 45, 99, 70, 50, 0).vel(4);
    m.op[2] = car(76).env(99, 70, 40, 45, 99, 40, 0, 0).vel(3);
    m.op[3] = mod(70).ratio(14).env(99, 75, 40, 45, 99, 30, 0, 0).vel(4);
    m.chordOf({0, 4, 10, 14}).pattern(5, 1);  // a lazy ninth, in the order played
    compose(m, {AL_BREEZE, 25, 15, 15, AL_COAST, 35, 30, 70, 15, 76, 2});
    all.push_back(m);
  }
  {  // August: thunder over the mountains. Brass that swells against the storm.
    Month m{"THUNDER", "August", "Thunder", "Brass against the storm",
            0x5A6F8F, 0xC9B98F, 0xB8A64A, 0x5E6D7A, 0x8A3A0F, 2};
    m.spread = 1;
    m.peg[0] = 70, m.peg[4] = 47;
    m.op[0] = car(96).env(70, 45, 35, 50, 99, 95, 92, 0).vel(2);
    m.op[1] = mod(84).env(55, 40, 35, 50, 99, 92, 88, 0).vel(3);
    m.op[2] = car(90).env(70, 45, 35, 50, 99, 95, 92, 0).vel(2);
    m.op[3] = mod(80).env(50, 40, 35, 50, 99, 90, 85, 0).vel(3);
    m.chordOf({-12, 0, 7, 10}).pattern(0, 1);  // a low root under a seventh, driving
    compose(m, {AL_STORM, 45, 45, 40, AL_MOUNTAINS, 40, 30, 45, 0, 132, 3});
    all.push_back(m);
  }
  {  // September: harvest in the village. A warm harp.
    Month m{"HARVEST", "September", "Harvest", "Warm harp at harvest",
            0x8FC1E3, 0xFFE8C2, 0xD9A93E, 0x9AB09A, 0xA35A12, 3};
    m.decays = true;
    m.spread = 1;
    m.op[0] = car(98).env(99, 40, 28, 45, 99, 72, 0, 0).vel(3);
    m.op[1] = mod(72).ratio(2).env(99, 55, 35, 45, 99, 55, 0, 0).vel(3);
    m.op[2] = car(80).env(99, 45, 30, 45, 99, 70, 0, 0).vel(3);
    m.op[3] = mod(60).ratio(3).env(99, 65, 40, 45, 99, 40, 0, 0).vel(3);
    m.chordOf({0, 7, 12, 16}).pattern(0, 2);  // a plain major, strummed upwards
    compose(m, {AL_BREEZE, 30, 25, 30, AL_VILLAGE, 30, 20, 60, 0, 92, 1});
    all.push_back(m);
  }
  {  // October: leaves falling in the wood. Soft vibes, a little sad.
    Month m{"LEAVES", "October", "Falling Leaves", "Soft vibes, leaves falling",
            0x9CB6D4, 0xF7D6B0, 0xC9662E, 0x8E7A6A, 0xB4441A, 3};
    m.decays = true;
    m.op[0] = car(97).env(99, 30, 22, 40, 99, 70, 0, 0).vel(3);
    m.op[1] = mod(60).ratio(4).env(99, 50, 30, 40, 99, 40, 0, 0).vel(3);
    m.op[2] = car(78).ratio(4).env(99, 50, 30, 40, 99, 40, 0, 0).vel(3);
    m.op[3] = mod(40).env(99, 50, 30, 40, 99, 40, 0, 0).vel(3);
    m.chordOf({0, 3, 7, 10}).pattern(1, 2);  // minor seventh, leaves falling
    compose(m, {AL_BREEZE, 45, 35, 45, AL_FOREST, 40, 30, 35, 25, 80, 1});
    all.push_back(m);
  }
  {  // November: mist on the lake. A dark, hollow pad.
    Month m{"MIST", "November", "Mist", "Dark pad in the mist",
            0x8E959C, 0xC9C6BF, 0x6E5F4C, 0x7E8288, 0x4F4438, 3};
    m.spread = 2;
    m.op[0] = car(99).ratio(0).env(58, 30, 30, 35, 99, 96, 94, 0).vel(2);
    m.op[1] = mod(76).env(50, 30, 30, 35, 99, 90, 88, 0).vel(2);
    m.op[2] = car(90).env(58, 30, 30, 35, 99, 95, 92, 0).vel(2);
    m.op[3] = mod(62).env(55, 30, 30, 35, 99, 90, 85, 0).vel(2);
    m.chordOf({-12, 0, 3, 7}).pattern(2, 1);  // low minor, slow
    compose(m, {AL_FOG, 60, 35, 20, AL_LAKE, 55, 20, 20, 0, 60, 0});
    all.push_back(m);
  }
  {  // December: candlelight in a snowy village. A choir with a celesta glint.
    Month m{"CANDLELITE", "December", "Candlelight", "Choir and celesta in the snow",
            0x1E2A4A, 0x4A5A80, 0xEEF2F8, 0x5A6788, 0xE8B04A, 0};
    m.op[0] = car(95).env(50, 40, 35, 40, 99, 96, 94, 0).vel(2);
    m.op[1] = mod(62).ratio(2).env(45, 35, 35, 40, 99, 90, 85, 0).vel(2);
    m.op[2] = car(76).ratio(4).env(99, 40, 30, 45, 99, 50, 0, 0).vel(3);
    m.op[3] = mod(55).ratio(9).env(99, 50, 35, 45, 99, 40, 0, 0).vel(3);
    m.chordOf({0, 7, 11, 16}).pattern(0, 3);  // major seventh, like bells up a tower
    compose(m, {AL_SNOW, 35, 25, 20, AL_VILLAGE, 50, 15, 75, 20, 88, 3});
    all.push_back(m);
  }
  for (int i = 0; i < (int)all.size(); i++) all[i].day[AL_MONTH] = i;
  return all;
}

// The weather under the notes. The FM-1 has no noise source, and its feedback is too weak to make one,
// so the weather is tuned to the key: it follows the music instead of hissing under it.
std::vector<Sky> buildSkies() {
  std::vector<Sky> all(AL_SKY_COUNT);
  // A clear sky is quiet: the weather stack is silent.
  all[AL_CLEAR] = {"Clear", car(0), mod(0), 0, 0, 20, 40, 0, 0, 0};
  // Wind: a breathy whistle two octaves up that swells in slowly and gusts with the LFO.
  all[AL_BREEZE] = {"Breeze", car(0).ratio(4).detune(9).env(52, 30, 20, 45, 99, 90, 85, 0).amp(2),
                    mod(55).ratio(4).env(60, 99, 99, 99, 99, 99, 99, 0), 7, 0, 6, 40, 30, 72, 99};
  // Rain: glassy drips high above the note, pattering with a sample and hold LFO.
  all[AL_RAIN] = {"Rain", car(0).ratio(8).env(90, 45, 30, 60, 99, 90, 85, 0).amp(2),
                  mod(62).ratio(11).env(99, 99, 99, 99, 99, 99, 99, 0), 0, 5, 60, 95, 70, 84, 99};
  // Fog: a soft hum an octave down, like a foghorn far off.
  all[AL_FOG] = {"Fog", car(0).ratio(0).env(50, 20, 20, 42, 99, 95, 95, 0).amp(1),
                 mod(58).ratio(0).env(99, 99, 99, 99, 99, 99, 99, 0), 5, 0, 10, 30, 15, 66, 93};
  // Storm: a rough, inharmonic growl an octave down that rolls in and away, shaken by a sample and hold LFO.
  all[AL_STORM] = {"Storm", car(0).ratio(0).env(60, 22, 18, 40, 99, 85, 70, 0).amp(2),
                   mod(86).ratio(0, 41).env(99, 99, 99, 99, 99, 99, 99, 0), 7, 5, 25, 60, 30, 74, 99};
  // Snow: a faint glint high up that drifts in.
  all[AL_SNOW] = {"Snow", car(0).ratio(6).env(50, 20, 20, 42, 99, 95, 95, 0).amp(1),
                  mod(45).ratio(13).env(99, 99, 99, 99, 99, 99, 99, 0), 0, 0, 8, 30, 15, 70, 95};
  return all;
}

}  // namespace

const std::vector<Month>& months() {
  static const std::vector<Month> all = build();
  return all;
}

const Sky& sky(int index) {
  static const std::vector<Sky> all = buildSkies();
  return all[std::max(0, std::min(AL_SKY_COUNT - 1, index))];
}

namespace {

void writeOp(const Op& o, uint8_t* p) {
  for (int k = 0; k < 4; k++) {
    p[k] = o.r[k];
    p[4 + k] = o.l[k];
  }
  p[8] = o.bp;
  p[9] = o.ld;
  p[10] = o.rd;
  p[11] = o.lc;
  p[12] = o.rc;
  p[13] = o.rs;
  p[14] = o.ams;
  p[15] = o.kvs;
  p[16] = o.ol;
  p[17] = o.mode;
  p[18] = o.coarse;
  p[19] = o.fine;
  p[20] = o.det;
}

}  // namespace

void baseVoice(const Month& m, const Sky& s, uint8_t v[155]) {
  memset(v, 0, 155);
  // VCED stores OP6 first.
  for (int i = 0; i < 4; i++) writeOp(m.op[i], v + (5 - i) * 21);
  writeOp(s.carrier, v + 1 * 21);
  writeOp(s.modulator, v + 0 * 21);
  // The two stacks of music are pulled apart by the month's spread.
  for (int i = 0; i < 4; i++) {
    uint8_t* p = v + (5 - i) * 21;
    p[20] = (uint8_t)std::max(0, std::min(14, (int)p[20] + (i < 2 ? -m.spread : m.spread)));
  }
  for (int k = 0; k < 8; k++) v[126 + k] = m.peg[k];
  v[134] = 4;  // algorithm 5
  v[135] = (uint8_t)s.fb;
  v[136] = 1;  // oscillator key sync
  v[137] = (uint8_t)s.lfoSlow;
  v[138] = (uint8_t)m.lfoDelay;
  v[139] = 0;  // vibrato depth: Shimmer
  v[140] = (uint8_t)s.amd;
  v[141] = 0;  // the LFO runs free, so the weather does not restart with every key
  v[142] = (uint8_t)s.lfoWave;
  v[143] = (uint8_t)m.pms;
  v[144] = (uint8_t)m.transpose;
  size_t n = strlen(m.voiceName);
  for (int i = 0; i < 10; i++) v[145 + i] = (uint8_t)(i < (int)n ? m.voiceName[i] : ' ');
}

void packVoice(const uint8_t v[155], uint8_t out[128]) {
  for (int op = 0; op < 6; op++) {
    const uint8_t* p = v + op * 21;
    uint8_t* o = out + op * 17;
    memcpy(o, p, 11);
    o[11] = (uint8_t)((p[12] << 2) | p[11]);
    o[12] = (uint8_t)((p[20] << 3) | p[13]);
    o[13] = (uint8_t)((p[15] << 2) | p[14]);
    o[14] = p[16];
    o[15] = (uint8_t)((p[18] << 1) | p[17]);
    o[16] = p[19];
  }
  memcpy(out + 102, v + 126, 8);
  out[110] = v[134];
  out[111] = (uint8_t)((v[136] << 3) | v[135]);
  out[112] = v[137];
  out[113] = v[138];
  out[114] = v[139];
  out[115] = v[140];
  out[116] = (uint8_t)((v[143] << 4) | (v[142] << 1) | v[141]);
  out[117] = v[144];
  memcpy(out + 118, v + 145, 10);
}

}  // namespace almanac
