#pragma once
#include <cstdint>
#include <vector>

#include "ArpeggiCore.h"

namespace arpeggi {

// One operator as it is voiced. A ratio is written times 100 (350 is 3.5); Bright moves the level of the operators
// marked bright, and Decay moves every operator's decay rates.
struct Op {
  int ratio = 100;
  int level = 0;
  int r[4] = {99, 99, 99, 99};
  int l[4] = {99, 99, 99, 0};
  bool bright = false;
  int detune = 7;
  int rateScale = 0;
  int ams = 0;
};

struct Sound {
  const char* name;
  int algorithm;  // 1 .. 32, as the DX7 numbers them
  int feedback;
  Op op[6];       // OP1 first
  int transpose = 0;
  int lfoSpeed = 35, lfoDelay = 0, pmd = 0, amd = 0, lfoWave = 4, pms = 0;
  bool lfoSync = false;
  int roomType = 1;  // 0 room, 1 hall, 2 plate
  int chorus = 0;    // chorus mix, 0 for none
};

const Sound& sound(int index);

struct Scene {
  const char* voiceName;  // 10 characters at most
  const char* title;
  const char* blurb;
  std::vector<int> seed;  // the notes PLAY starts with when the pool is empty
  int32_t s[AP_FIELDS] = {};
};

const std::vector<Scene>& scenes();
void packVoice(const uint8_t v[155], uint8_t out[128]);

}  // namespace arpeggi
