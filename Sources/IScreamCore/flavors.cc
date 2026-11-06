#include <algorithm>
#include <cstring>

#include "internal.h"

namespace iscream {

namespace {

// How the toppings default for a flavor: on, then knobs A, B and C.
struct Top {
  int on, a, b, c;
};

void sundae(Flavor& f, int coneStyle, int toast, int crunch, Top cream, Top fudge, Top caramel, Top chips, Top sprinkles,
            int sweet = 50, int melt = 50, int fizz = 0) {
  int32_t* s = f.sundae;
  s[IS_FLAVOR] = 0;
  s[IS_SCOOPS] = 1;
  s[IS_SWEET] = sweet;
  s[IS_MELT] = melt;
  s[IS_FIZZ] = fizz;
  s[IS_CONE_ON] = 1;
  s[IS_CONE_STYLE] = coneStyle;
  s[IS_TOAST] = toast;
  s[IS_CRUNCH] = crunch;
  const Top t[IS_TOPPING_COUNT] = {cream, fudge, caramel, chips, sprinkles};
  for (int i = 0; i < IS_TOPPING_COUNT; i++) {
    s[IS_TOP(i, 0)] = t[i].on;
    s[IS_TOP(i, 1)] = t[i].a;
    s[IS_TOP(i, 2)] = t[i].b;
    s[IS_TOP(i, 3)] = t[i].c;
  }
  // The sprinkles' third knob picks the room, not a level.
  if (s[IS_TOP(IS_SPRINKLES, 3)] > 2) s[IS_TOP(IS_SPRINKLES, 3)] = 1;
}

constexpr Top off(int a = 40, int b = 40, int c = 40) { return {0, a, b, c}; }
constexpr Top on(int a, int b, int c) { return {1, a, b, c}; }

// The flavors. Each is a DX7 voice built from three stacks of two operators (algorithm 5), so
// "scoops" can detune the stacks against each other and widen it. Modulator levels are written so
// that Sweet at 50 is as designed; carriers are balanced for about equal loudness across flavors.
std::vector<Flavor> build() {
  std::vector<Flavor> all;

  {  // Vanilla: the plain one. A saw-like analog voice that widens into a super saw.
    Flavor f{"VANILLA", "Vanilla", "Plain analog saw", 0xFFF1C9, 0xE6C97A};
    f.alg = 5, f.fb = 4;
    f.op[0] = car(93, -1).env(92, 55, 45, 50, 99, 97, 96, 0).vel(2);
    f.op[1] = mod(90, -1).env(92, 45, 40, 50, 99, 97, 95, 0).vel(2);
    f.op[2] = car(93, 0).env(92, 55, 45, 50, 99, 97, 96, 0).vel(2);
    f.op[3] = mod(90, 0).env(92, 45, 40, 50, 99, 97, 95, 0).vel(2);
    f.op[4] = car(93, 1).env(92, 55, 45, 50, 99, 97, 96, 0).vel(2);
    f.op[5] = mod(90, 1).env(92, 45, 40, 50, 99, 97, 95, 0).vel(2);
    sundae(f, 0, 88, 8, on(35, 30, 45), off(), off(), off(), on(18, 40, 1));
    all.push_back(f);
  }
  {  // Chocolate: hollow and pulse-like, from operators an octave apart; two stacks beat like PWM.
    Flavor f{"CHOCOLATE", "Chocolate", "Hollow pulse, slow PWM beat", 0x7A4524, 0x4A2410};
    f.alg = 5, f.fb = 0;
    f.baseSpread = 1;
    f.op[0] = car(98, -1).env(95, 60, 50, 52, 99, 98, 97, 0).vel(2);
    f.op[1] = mod(86, -1).ratio(2).env(95, 50, 40, 50, 99, 97, 95, 0).vel(2);
    f.op[2] = car(98, 0).env(95, 60, 50, 52, 99, 98, 97, 0).vel(2);
    f.op[3] = mod(86, 0).ratio(2).env(95, 50, 40, 50, 99, 97, 95, 0).vel(2);
    f.op[4] = car(98, 1).env(95, 60, 50, 52, 99, 98, 97, 0).vel(2);
    f.op[5] = mod(86, 1).ratio(2).env(95, 50, 40, 50, 99, 97, 95, 0).vel(2);
    sundae(f, 0, 76, 22, off(), on(28, 55, 90), off(), off(), on(14, 35, 0));
    all.push_back(f);
  }
  {  // Strawberry: sweet, bright pluck lead.
    Flavor f{"STRAWBERRY", "Strawberry", "Sweet, bright pluck", 0xFF4D6D, 0xC81E48};
    f.alg = 5, f.fb = 3, f.decays = true;
    f.op[0] = car(99, -1).env(98, 40, 32, 52, 99, 96, 82, 0).vel(3);
    f.op[1] = mod(94, -1).env(98, 45, 38, 50, 99, 90, 76, 0).vel(3);
    f.op[2] = car(88, 0).ratio(2).env(98, 42, 34, 52, 99, 90, 70, 0).vel(3);
    f.op[3] = mod(86, 0).env(98, 48, 38, 50, 99, 86, 66, 0).vel(3);
    f.op[4] = car(99, 1).env(98, 40, 32, 52, 99, 96, 82, 0).vel(3);
    f.op[5] = mod(84, 1).ratio(3).env(98, 55, 40, 50, 99, 80, 62, 0).vel(3);
    sundae(f, 0, 94, 12, on(30, 35, 45), off(), off(), on(24, 32, 52), on(20, 45, 0), 50, 50, 8);
    all.push_back(f);
  }
  {  // Mint Chip: icy glass bells.
    Flavor f{"MINT CHIP", "Mint Chip", "Icy glass bells", 0x8FF0D0, 0x2BB892};
    f.alg = 5, f.fb = 0, f.decays = true;
    f.baseSpread = 1;
    f.op[0] = car(99, -1).env(99, 36, 28, 42, 99, 90, 0, 0).vel(3);
    f.op[1] = mod(88, -1).ratio(3, 50).env(99, 42, 28, 40, 99, 82, 0, 0).vel(3);
    f.op[2] = car(92, 0).ratio(4).env(99, 52, 36, 50, 99, 78, 0, 0).vel(3);
    f.op[3] = mod(70, 0).ratio(1).env(99, 58, 38, 50, 99, 60, 0, 0).vel(3);
    f.op[4] = car(96, 1).env(99, 34, 28, 42, 99, 88, 0, 0).vel(3);
    f.op[5] = mod(80, 1).ratio(7).env(99, 68, 48, 55, 99, 56, 0, 0).vel(3);
    sundae(f, 2, 12, 10, off(), off(), off(), on(30, 38, 48), on(38, 62, 1), 50, 50, 0);
    all.push_back(f);
  }
  {  // Cotton Candy: airy, shimmering pad.
    Flavor f{"COTTONCNDY", "Cotton Candy", "Airy, shimmering pad", 0xA8E6FF, 0xFF9AD5};
    f.alg = 5, f.fb = 2;
    f.baseSpread = 2;
    f.op[0] = car(97, -1).env(62, 50, 50, 46, 99, 99, 99, 0);
    f.op[1] = mod(88, -1).ratio(7).env(52, 50, 50, 50, 99, 96, 92, 0);
    f.op[2] = car(92, 0).ratio(2).env(60, 50, 50, 46, 99, 99, 99, 0);
    f.op[3] = mod(86, 0).ratio(3).env(50, 50, 50, 50, 99, 96, 92, 0);
    f.op[4] = car(97, 1).env(64, 50, 50, 46, 99, 99, 99, 0);
    f.op[5] = mod(90, 1).env(52, 50, 50, 50, 99, 98, 94, 0);
    sundae(f, 0, 100, 0, on(55, 22, 70), off(), on(35, 25, 60), off(), on(38, 70, 2), 50, 50, 10);
    all.push_back(f);
  }
  {  // Bubblegum: a bouncy, popping mono voice with a quick pitch bloop.
    Flavor f{"BUBBLEGUM", "Bubblegum", "Bouncy bloop", 0xFF9AE2, 0xDE55BA};
    f.alg = 5, f.fb = 3, f.mono = true, f.decays = true;
    f.peg[0] = 80, f.peg[1] = 99, f.peg[2] = 99, f.peg[3] = 99;
    f.peg[4] = 50, f.peg[5] = 50, f.peg[6] = 50, f.peg[7] = 36;
    f.op[0] = car(99, -1).env(99, 52, 38, 60, 99, 96, 62, 0).vel(2);
    f.op[1] = mod(92, -1).env(99, 54, 36, 60, 99, 82, 54, 0).vel(2);
    f.op[2] = car(99, 0).ratio(0).env(99, 60, 45, 60, 99, 97, 76, 0).vel(2);
    f.op[3] = mod(76, 0).env(99, 60, 45, 60, 99, 72, 50, 0).vel(2);
    f.op[4] = car(88, 1).ratio(2).env(99, 66, 40, 60, 99, 82, 34, 0).vel(2);
    f.op[5] = mod(86, 1).env(99, 62, 40, 60, 99, 62, 36, 0).vel(2);
    sundae(f, 0, 100, 0, off(), off(), off(), on(22, 30, 58), on(18, 40, 0));
    all.push_back(f);
  }
  {  // Rocky Road: gritty, chunky mono bass.
    Flavor f{"ROCKY ROAD", "Rocky Road", "Gritty, chunky bass", 0x5B3A4A, 0x2E1A26};
    f.alg = 5, f.fb = 7, f.mono = true, f.transpose = 12;
    f.op[0] = car(99, -1).env(99, 62, 48, 55, 99, 95, 92, 0).vel(2);
    f.op[1] = mod(99, -1).env(99, 56, 46, 55, 99, 95, 90, 0).vel(2);
    f.op[2] = car(99, 0).ratio(0).env(99, 70, 55, 55, 99, 97, 96, 0).vel(1);
    f.op[3] = mod(60, 0).env(99, 60, 50, 55, 99, 80, 70, 0);
    f.op[4] = car(90, 1).env(99, 62, 48, 55, 99, 95, 92, 0).vel(2);
    f.op[5] = mod(98, 1).ratio(3).env(99, 58, 46, 55, 99, 85, 78, 0).vel(2);
    sundae(f, 0, 62, 34, off(), on(55, 52, 92), off(), off(), off());
    all.push_back(f);
  }
  {  // Cookies & Cream: a warm electric piano.
    Flavor f{"COOKIES&CR", "Cookies & Cream", "Warm tine e-piano", 0xEFE9E4, 0x3A3030};
    f.alg = 5, f.fb = 0, f.decays = true;
    f.baseSpread = 1;
    f.op[0] = car(99, -1).env(98, 42, 25, 48, 99, 96, 0, 0).vel(3);
    f.op[1] = mod(92, -1).env(99, 48, 35, 50, 99, 80, 48, 0).vel(4);
    f.op[2] = car(90, 0).env(99, 70, 40, 60, 99, 84, 0, 0).vel(3);
    f.op[3] = mod(90, 0).ratio(14).env(99, 74, 50, 60, 99, 40, 0, 0).vel(4);
    f.op[4] = car(96, 1).env(98, 40, 24, 48, 99, 96, 0, 0).vel(3);
    f.op[5] = mod(70, 1).env(99, 50, 35, 50, 99, 80, 50, 0).vel(3);
    sundae(f, 0, 88, 6, on(32, 22, 42), off(), off(), off(), on(18, 40, 0));
    all.push_back(f);
  }
  {  // Butter Pecan: woody marimba pluck.
    Flavor f{"BUTTERPCAN", "Butter Pecan", "Woody marimba pluck", 0xF2C98A, 0xB07838};
    f.alg = 5, f.fb = 0, f.decays = true;
    f.op[0] = car(99, -1).env(99, 54, 40, 60, 99, 92, 0, 0).vel(3);
    f.op[1] = mod(86, -1).ratio(4).env(99, 66, 48, 60, 99, 70, 0, 0).vel(3);
    f.op[2] = car(94, 0).ratio(4).env(99, 74, 48, 60, 99, 60, 0, 0).vel(3);
    f.op[3] = mod(76, 0).env(99, 70, 48, 60, 99, 60, 0, 0).vel(3);
    f.op[4] = car(98, 1).env(99, 52, 38, 58, 99, 92, 0, 0).vel(3);
    f.op[5] = mod(82, 1).env(99, 62, 44, 60, 99, 68, 0, 0).vel(3);
    sundae(f, 0, 96, 4, off(), off(), on(30, 30, 40), off(), on(22, 36, 0), 50, 50, 0);
    all.push_back(f);
  }
  {  // Lemon Zest: a sharp, buzzy brass stab.
    Flavor f{"LEMON ZEST", "Lemon Zest", "Zesty brass stab", 0xFFE95C, 0xE0B800};
    f.alg = 5, f.fb = 5;
    f.op[0] = car(99, -1).env(72, 50, 50, 55, 99, 99, 98, 0).vel(4);
    f.op[1] = mod(94, -1).env(66, 48, 50, 55, 99, 94, 90, 0).vel(5);
    f.op[2] = car(99, 0).env(72, 50, 50, 55, 99, 99, 98, 0).vel(4);
    f.op[3] = mod(92, 0).env(64, 48, 50, 55, 99, 94, 90, 0).vel(5);
    f.op[4] = car(99, 1).env(72, 50, 50, 55, 99, 99, 98, 0).vel(4);
    f.op[5] = mod(94, 1).env(66, 48, 50, 55, 99, 94, 90, 0).vel(5);
    sundae(f, 0, 98, 8, off(), on(22, 60, 72), off(), off(), on(14, 35, 0));
    all.push_back(f);
  }
  {  // Blueberry: a deep, dreamy pad.
    Flavor f{"BLUEBERRY", "Blueberry", "Deep, dreamy pad", 0x6C7BFF, 0x3040C8};
    f.alg = 5, f.fb = 2;
    f.baseSpread = 2;
    f.op[0] = car(94, -1).env(58, 50, 50, 44, 99, 99, 99, 0);
    f.op[1] = mod(90, -1).env(48, 50, 50, 48, 99, 96, 94, 0);
    f.op[2] = car(92, 0).ratio(0).env(60, 50, 50, 44, 99, 99, 99, 0);
    f.op[3] = mod(84, 0).env(46, 50, 50, 48, 99, 96, 94, 0);
    f.op[4] = car(94, 1).env(58, 50, 50, 44, 99, 99, 99, 0);
    f.op[5] = mod(92, 1).env(48, 50, 50, 48, 99, 96, 94, 0);
    sundae(f, 0, 70, 12, on(60, 15, 60), off(), off(), on(18, 32, 45), on(40, 75, 1), 50, 50, 8);
    all.push_back(f);
  }
  {  // Mango: a steel drum.
    Flavor f{"MANGO", "Mango", "Tropical steel drum", 0xFFB02E, 0xE86A0A};
    f.alg = 5, f.fb = 0, f.decays = true;
    f.op[0] = car(99, -1).env(99, 52, 36, 50, 99, 92, 0, 0).vel(3);
    f.op[1] = mod(90, -1).ratio(2).env(99, 62, 40, 50, 99, 74, 0, 0).vel(3);
    f.op[2] = car(96, 0).ratio(2).env(99, 54, 38, 50, 99, 86, 0, 0).vel(3);
    f.op[3] = mod(76, 0).env(99, 66, 44, 50, 99, 66, 0, 0).vel(3);
    f.op[4] = car(84, 1).ratio(3).env(99, 70, 46, 55, 99, 70, 0, 0).vel(3);
    f.op[5] = mod(90, 1).ratio(5).env(99, 80, 50, 60, 99, 50, 0, 0).vel(3);
    sundae(f, 0, 96, 6, off(), off(), off(), on(28, 40, 52), on(22, 42, 0));
    all.push_back(f);
  }

  // What is mixed into each scoop: chips, specks, swirls.
  const struct {
    uint32_t bits;
    int pattern;
  } look[] = {
      {0xB89A5A, 1},  // vanilla: bean specks
      {0x4A2410, 0},  // chocolate: smooth
      {0xFF9FB6, 2},  // strawberry: fruit pieces
      {0x4A2C1D, 2},  // mint chip: chocolate chips
      {0xFF9AD5, 3},  // cotton candy: pink swirl
      {0xFFFFFF, 3},  // bubblegum: white swirl
      {0xFFFFFF, 2},  // rocky road: marshmallows
      {0x3A3030, 2},  // cookies & cream: cookie crumbs
      {0xB07838, 2},  // butter pecan: pecans
      {0xFFFFFF, 0},  // lemon: smooth
      {0xA8B0FF, 3},  // blueberry: lighter swirl
      {0xFF7A1A, 3},  // mango: deeper swirl
  };
  for (size_t i = 0; i < all.size(); i++) {
    all[i].sundae[IS_FLAVOR] = (int32_t)i;
    all[i].bits = look[i].bits;
    all[i].pattern = look[i].pattern;
  }
  return all;
}

}  // namespace

const std::vector<Flavor>& flavors() {
  static const std::vector<Flavor> all = build();
  return all;
}

int scoopSpread(int scoops) {
  static const int steps[3] = {0, 2, 5};
  return steps[std::max(1, std::min(3, scoops)) - 1];
}

void baseVoice(const Flavor& f, uint8_t v[155]) {
  memset(v, 0, 155);
  for (int i = 0; i < 6; i++) {
    const Op& o = f.op[i];
    uint8_t* p = v + (5 - i) * 21;  // VCED stores OP6 first
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
    p[17] = 0;
    p[18] = o.coarse;
    p[19] = o.fine;
    p[20] = (uint8_t)std::max(0, std::min(14, o.det + o.spread * f.baseSpread));
  }
  for (int k = 0; k < 8; k++) v[126 + k] = f.peg[k];
  v[134] = (uint8_t)(f.alg - 1);
  v[135] = (uint8_t)f.fb;
  v[136] = 1;  // oscillator key sync
  v[137] = (uint8_t)f.lfoSpeed;
  v[138] = (uint8_t)f.lfoDelay;
  v[139] = 0;  // vibrato depth: Fizz
  v[140] = 0;
  v[141] = 1;
  v[142] = 4;  // sine
  v[143] = (uint8_t)f.pms;
  v[144] = (uint8_t)f.transpose;
  size_t n = strlen(f.voiceName);
  for (int i = 0; i < 10; i++) v[145 + i] = (uint8_t)(i < (int)n ? f.voiceName[i] : ' ');
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

}  // namespace iscream
