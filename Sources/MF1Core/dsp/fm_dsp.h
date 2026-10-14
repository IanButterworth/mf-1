/*
 * Copyright 2012 Google Inc.
 * Modifications copyright 2026 the MF-1 authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// DX7-compatible fixed point FM building blocks, derived from Google's
// music-synthesizer-for-android (msfa). Levels are log2 in Q24, phases are
// Q24 per cycle, and control values update once per block of N samples.

#pragma once
#include <cstdint>

namespace mf1 {

// 64 sample control blocks, as in the FM-1 firmware.
constexpr int LG_N = 6;
constexpr int N = 1 << LG_N;

// Tables are shared and built once, before any audio runs.
struct Tables {
  static void init(double sample_rate);
  static double sampleRate;
  // Envelope rates are calibrated at 44.1 kHz; this rescales them (Q24).
  static int32_t srMultiplier;
};

namespace Sin {
constexpr int LG_SAMPLES = 10;
constexpr int SAMPLES = 1 << LG_SAMPLES;
extern int32_t tab[SAMPLES << 1];
inline int32_t lookup(int32_t phase) {
  constexpr int SHIFT = 24 - LG_SAMPLES;
  int lowbits = phase & ((1 << SHIFT) - 1);
  int ix = (phase >> (SHIFT - 1)) & ((SAMPLES - 1) << 1);
  int dy = tab[ix];
  int y0 = tab[ix + 1];
  return y0 + (int32_t)(((int64_t)dy * (int64_t)lowbits) >> SHIFT);
}
}  // namespace Sin

namespace Exp2 {
constexpr int LG_SAMPLES = 10;
constexpr int SAMPLES = 1 << LG_SAMPLES;
extern int32_t tab[SAMPLES << 1];
// Q24 log2 in, Q24 linear out.
inline int32_t lookup(int32_t x) {
  constexpr int SHIFT = 24 - LG_SAMPLES;
  int lowbits = x & ((1 << SHIFT) - 1);
  int ix = (x >> (SHIFT - 1)) & ((SAMPLES - 1) << 1);
  int dy = tab[ix];
  int y0 = tab[ix + 1];
  int y = y0 + (int)(((int64_t)dy * (int64_t)lowbits) >> SHIFT);
  int sh = 6 - (x >> 24);
  if (sh >= 31) return 0;
  return y >> sh;
}
}  // namespace Exp2

namespace Freqlut {
// Q24 log2 frequency in Hz to Q24 phase increment per sample.
int32_t lookup(int32_t logfreq);
}

class Env {
 public:
  // rates and levels are DX7 0..99 values; outlevel is in microsteps
  // (about 0.0235 dB, 99 * 32 nominal full scale); rate_scaling in qrate units.
  void init(const int rates[4], const int levels[4], int outlevel, int rate_scaling);
  void update(const int rates[4], const int levels[4], int outlevel, int rate_scaling);
  int32_t getsample();
  void keydown(bool down);
  bool isIdle() const { return ix_ == 4; }
  int stage() const { return ix_; }
  static int scaleoutlevel(int outlevel);
  // Restart from the current level, as the DX7 does when a voice is retriggered.
  void retrigger();

 private:
  int rates_[4];
  int levels_[4];
  int outlevel_;
  int rate_scaling_;
  int32_t level_ = 0;
  int32_t targetlevel_ = 0;
  bool rising_ = false;
  int ix_ = 4;
  int32_t inc_ = 0;
  bool down_ = false;
  int32_t staticcount_ = 0;
  void advance(int newix);
};

class PitchEnv {
 public:
  static void init(double sample_rate);
  // slow stretches every rate, as measured on FM-1 V15 (1.37x).
  void set(const int rates[4], const int levels[4], bool slow = false);
  void update(const int rates[4], const int levels[4]);
  // Q24 per octave.
  int32_t getsample();
  void keydown(bool down);

 private:
  static int unit_;
  int rates_[4];
  int levels_[4];
  int32_t level_ = 0;
  int32_t targetlevel_ = 0;
  bool rising_ = false;
  int ix_ = 4;
  int32_t inc_ = 0;
  bool down_ = false;
  bool slow_ = false;
  void advance(int newix);
};

class Lfo {
 public:
  static void init(double sample_rate);
  // params: speed, delay, pmd, amd, sync, wave (VCED 137..142 order)
  void reset(const uint8_t params[6]);
  // 0..1 in Q24
  int32_t getsample();
  // Delay ramp, 0..1 in Q24.
  int32_t getdelay();
  void keydown();

 private:
  static uint32_t unit_;
  uint32_t phase_ = 0;
  uint32_t delta_ = 0;
  uint8_t waveform_ = 0;
  uint8_t randstate_ = 0;
  bool sync_ = false;
  uint32_t delaystate_ = 0;
  uint32_t delayinc_ = 0;
  uint32_t delayinc2_ = 0;
};

struct FmOpParams {
  int32_t gain[2];
  int32_t freq;
  int32_t phase;
};

class FmCore {
 public:
  // Ops are in VCED order: index 0 is OP6.
  void compute(int32_t* output, FmOpParams* params, int algorithm, int32_t* fb_buf,
               int feedback_shift);
  // Bit i set means op (i+1) is a carrier for that algorithm (0-based algorithm).
  static int carrierMask(int algorithm);
  static int carrierCount(int algorithm);

 private:
  alignas(16) int32_t buf_[2][N];
};

}  // namespace mf1
