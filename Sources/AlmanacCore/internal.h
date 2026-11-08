#pragma once
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "AlmanacCore.h"

namespace almanac {

// One DX7 operator. OP1 is listed first (VCED stores OP6 first).
struct Op {
  uint8_t r[4] = {99, 99, 99, 99}, l[4] = {99, 99, 99, 0};
  uint8_t ol = 0, mode = 0, coarse = 1, fine = 0, det = 7, kvs = 0, rs = 0, ams = 0;
  uint8_t bp = 39, ld = 0, rd = 0, lc = 0, rc = 0;
  // A modulator, which Warmth and Mood turn up and down. Carriers are never touched by them.
  bool mod = false;

  Op& env(int r1, int r2, int r3, int r4, int l1, int l2, int l3, int l4) {
    r[0] = r1, r[1] = r2, r[2] = r3, r[3] = r4;
    l[0] = l1, l[1] = l2, l[2] = l3, l[3] = l4;
    return *this;
  }
  Op& ratio(int c, int f = 0) {
    coarse = (uint8_t)c, fine = (uint8_t)f;
    return *this;
  }
  Op& detune(int d) {
    det = (uint8_t)d;
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
  Op& amp(int sensitivity) {
    ams = (uint8_t)sensitivity;
    return *this;
  }
  // Level scaling: darker (or quieter) up the keyboard.
  Op& scale(int right, int left = 0, int curveR = 0, int curveL = 0) {
    rd = (uint8_t)right, ld = (uint8_t)left, rc = (uint8_t)curveR, lc = (uint8_t)curveL;
    return *this;
  }
};

inline Op car(int ol) {
  Op o;
  o.ol = (uint8_t)ol;
  return o;
}
inline Op mod(int ol) {
  Op o = car(ol);
  o.mod = true;
  return o;
}

// A month's music: two stacks of two operators, OP1 <- OP2 and OP3 <- OP4 (algorithm 5). The third
// stack, OP5 <- OP6, belongs to the sky.
struct Month {
  const char* voiceName;  // 10 characters at most
  const char* month;
  const char* title;
  const char* blurb;
  uint32_t skyTop, skyBottom, ground, far, accent;
  int season;
  Op op[4];
  uint8_t peg[8] = {99, 99, 99, 99, 50, 50, 50, 50};  // rates 1-4, levels 1-4 (50 is no shift)
  int transpose = 24;                                 // 24 is no shift
  bool mono = false;
  // Detune, in steps, between the two stacks.
  int spread = 0;
  // Envelopes fall away by themselves (plucks and bells), so Linger stretches the decay too.
  bool decays = false;
  int lfoDelay = 20, pms = 3;
  // The month's chord, as semitones above the key, close voiced; up to five notes.
  int8_t chord[5] = {0, 7, 0, 0, 0};
  int chordSize = 2;
  // How its pattern runs on the arpeggiator: mode (0 up, 1 down, 2 inclusive, 4 random, 5 order) and octaves.
  int arpMode = 0, arpOctaves = 1;
  int32_t day[AL_FIELDS];

  Month& chordOf(std::initializer_list<int> notes) {
    chordSize = 0;
    for (int n : notes)
      if (chordSize < 5) chord[chordSize++] = (int8_t)n;
    return *this;
  }
  Month& pattern(int mode, int octaves) {
    arpMode = mode, arpOctaves = octaves;
    return *this;
  }
};

// The weather's own stack and how the sky moves it. OP5 is heard; OP6 roughens it with feedback.
struct Sky {
  const char* name;
  Op carrier, modulator;
  int fb;
  int lfoWave;            // 0 triangle, 5 sample and hold
  int lfoSlow, lfoFast;   // LFO speed at Gusts 0 and 100
  int amd;                // how much the LFO moves the weather's level
  int quiet, loud;        // OP5 output level at Elements 1 and 100 (0 is silent)
};

const std::vector<Month>& months();
const Sky& sky(int index);
// The 155 bytes of DX7 VCED for a month as composed, under its own sky.
void baseVoice(const Month& m, const Sky& s, uint8_t v[155]);
void packVoice(const uint8_t v[155], uint8_t out[128]);

}  // namespace almanac
