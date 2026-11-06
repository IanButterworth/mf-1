#pragma once
#include <cstdint>
#include <vector>

#include "IScreamCore.h"

namespace iscream {

// One DX7 operator, as the flavors list them. OP1 is listed first (VCED stores OP6 first).
struct Op {
  uint8_t r[4] = {99, 99, 99, 99}, l[4] = {99, 99, 99, 0};
  uint8_t ol = 0, coarse = 1, fine = 0, det = 7, kvs = 0, rs = 0, ams = 0;
  uint8_t bp = 39, ld = 0, rd = 0, lc = 0, rc = 0;
  // Which detune group the operator follows when the voice is widened: -1, 0 or +1.
  int8_t spread = 0;
  // A modulator, which Sweet turns up and down. Carriers are never touched by it.
  bool mod = false;

  Op& env(int r1, int r2, int r3, int r4, int l1, int l2, int l3, int l4) {
    r[0] = r1, r[1] = r2, r[2] = r3, r[3] = r4;
    l[0] = l1, l[1] = l2, l[2] = l3, l[3] = l4;
    return *this;
  }
  Op& ratio(int c, int f = 0) {
    coarse = (uint8_t)c;
    fine = (uint8_t)f;
    return *this;
  }
  Op& vel(int v) {
    kvs = (uint8_t)v;
    return *this;
  }
  Op& rateScale(int v) {
    rs = (uint8_t)v;
    return *this;
  }
  Op& detune(int d) {
    det = (uint8_t)d;
    return *this;
  }
  // Level scaling: darker (or quieter) up the keyboard.
  Op& scale(int right, int left = 0, int curveR = 0, int curveL = 0) {
    rd = (uint8_t)right, ld = (uint8_t)left, rc = (uint8_t)curveR, lc = (uint8_t)curveL;
    return *this;
  }
};

inline Op car(int ol, int spread = 0) {
  Op o;
  o.ol = (uint8_t)ol;
  o.spread = (int8_t)spread;
  return o;
}
inline Op mod(int ol, int spread = 0) {
  Op o = car(ol, spread);
  o.mod = true;
  return o;
}

struct Flavor {
  const char* voiceName;  // 10 characters at most
  const char* title;
  const char* blurb;
  uint32_t color, accent;
  uint32_t bits = 0;
  int pattern = 0;
  int alg = 5;
  int fb = 0;
  Op op[6];
  uint8_t peg[8] = {99, 99, 99, 99, 50, 50, 50, 50};  // rates 1-4, levels 1-4 (50 is no shift)
  int transpose = 24;                                 // 24 is no shift
  bool mono = false;
  // Detune, in steps, between the stacks of one scoop. Scoops add to it.
  int baseSpread = 0;
  // Envelopes fall away by themselves (plucks and bells), so Melt stretches the decay too.
  bool decays = false;
  int lfoSpeed = 35, lfoDelay = 25, pms = 3;
  int32_t sundae[IS_FIELDS];
};

const std::vector<Flavor>& flavors();
// The 155 bytes of DX7 VCED for a flavor as designed.
void baseVoice(const Flavor& f, uint8_t v[155]);
// Packs a VCED voice into the 128 bytes of a DX7 bank.
void packVoice(const uint8_t v[155], uint8_t out[128]);
// Detune steps between the stacks for 1, 2 and 3 scoops.
int scoopSpread(int scoops);

}  // namespace iscream
