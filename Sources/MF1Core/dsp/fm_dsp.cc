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

#include "fm_dsp.h"

#include <algorithm>
#include <cmath>

namespace mf1 {

double Tables::sampleRate = 48000;
int32_t Tables::srMultiplier = 1 << 24;

int32_t Sin::tab[Sin::SAMPLES << 1];
int32_t Exp2::tab[Exp2::SAMPLES << 1];

namespace {
constexpr int FREQ_LG_SAMPLES = 10;
constexpr int FREQ_SAMPLES = 1 << FREQ_LG_SAMPLES;
constexpr int FREQ_SHIFT = 24 - FREQ_LG_SAMPLES;
constexpr int MAX_LOGFREQ_INT = 20;
int32_t freqtab[FREQ_SAMPLES + 1];

void initSin() {
  double dphase = 2 * M_PI / Sin::SAMPLES;
  constexpr int64_t R = 1 << 29;
  int32_t c = (int32_t)std::floor(std::cos(dphase) * (1 << 30) + 0.5);
  int32_t s = (int32_t)std::floor(std::sin(dphase) * (1 << 30) + 0.5);
  int32_t u = 1 << 30;
  int32_t v = 0;
  for (int i = 0; i < Sin::SAMPLES / 2; i++) {
    Sin::tab[(i << 1) + 1] = (v + 32) >> 6;
    Sin::tab[((i + Sin::SAMPLES / 2) << 1) + 1] = -((v + 32) >> 6);
    int32_t t = (int32_t)(((int64_t)u * s + (int64_t)v * c + R) >> 30);
    u = (int32_t)(((int64_t)u * c - (int64_t)v * s + R) >> 30);
    v = t;
  }
  for (int i = 0; i < Sin::SAMPLES - 1; i++)
    Sin::tab[i << 1] = Sin::tab[(i << 1) + 3] - Sin::tab[(i << 1) + 1];
  Sin::tab[(Sin::SAMPLES << 1) - 2] = -Sin::tab[(Sin::SAMPLES << 1) - 1];
}

void initExp2() {
  double inc = std::exp2(1.0 / Exp2::SAMPLES);
  double y = 1 << 30;
  for (int i = 0; i < Exp2::SAMPLES; i++) {
    Exp2::tab[(i << 1) + 1] = (int32_t)std::floor(y + 0.5);
    y *= inc;
  }
  for (int i = 0; i < Exp2::SAMPLES - 1; i++)
    Exp2::tab[i << 1] = Exp2::tab[(i << 1) + 3] - Exp2::tab[(i << 1) + 1];
  Exp2::tab[(Exp2::SAMPLES << 1) - 2] = (int32_t)((1U << 31) - Exp2::tab[(Exp2::SAMPLES << 1) - 1]);
}

void initFreq(double sample_rate) {
  double y = (double)(1LL << (24 + MAX_LOGFREQ_INT)) / sample_rate;
  double inc = std::pow(2, 1.0 / FREQ_SAMPLES);
  for (int i = 0; i < FREQ_SAMPLES + 1; i++) {
    freqtab[i] = (int32_t)std::floor(y + 0.5);
    y *= inc;
  }
}
}  // namespace

void Tables::init(double sample_rate) {
  sampleRate = sample_rate;
  srMultiplier = (int32_t)((44100.0 / sample_rate) * (1 << 24));
  initSin();
  initExp2();
  initFreq(sample_rate);
  Lfo::init(sample_rate);
  PitchEnv::init(sample_rate);
}

int32_t Freqlut::lookup(int32_t logfreq) {
  if (logfreq < 0) return 0;
  int ix = (logfreq & 0xffffff) >> FREQ_SHIFT;
  int32_t y0 = freqtab[ix];
  int32_t y1 = freqtab[ix + 1];
  int lowbits = logfreq & ((1 << FREQ_SHIFT) - 1);
  int32_t y = y0 + (int32_t)(((int64_t)(y1 - y0) * lowbits) >> FREQ_SHIFT);
  int hibits = logfreq >> 24;
  if (hibits > MAX_LOGFREQ_INT) return y;
  return y >> (MAX_LOGFREQ_INT - hibits);
}

// ---------------------------------------------------------------- Env

static const int levellut[] = {0,  5,  9,  13, 17, 20, 23, 25, 27, 29,
                               31, 33, 35, 37, 39, 41, 42, 43, 45, 46};

int Env::scaleoutlevel(int outlevel) {
  return outlevel >= 20 ? 28 + outlevel : levellut[std::max(0, outlevel)];
}

void Env::init(const int r[4], const int l[4], int ol, int rate_scaling) {
  for (int i = 0; i < 4; i++) {
    rates_[i] = r[i];
    levels_[i] = l[i];
  }
  outlevel_ = ol;
  rate_scaling_ = rate_scaling;
  level_ = 0;
  down_ = true;
  advance(0);
}

void Env::retrigger() {
  down_ = true;
  advance(0);
}

void Env::update(const int r[4], const int l[4], int ol, int rate_scaling) {
  for (int i = 0; i < 4; i++) {
    rates_[i] = r[i];
    levels_[i] = l[i];
  }
  outlevel_ = ol;
  rate_scaling_ = rate_scaling;
  if (down_) {
    // Only the sustain stage is re-aimed; moving stages keep their course
    // so an edit while holding a key does not click.
    if (ix_ == 3) advance(3);
  }
}

int32_t Env::getsample() {
  if (staticcount_) {
    staticcount_ -= N;
    if (staticcount_ <= 0) {
      staticcount_ = 0;
      advance(ix_ + 1);
    }
  }
  if (ix_ < 3 || (ix_ < 4 && !down_)) {
    if (staticcount_) {
    } else if (rising_) {
      const int jumptarget = 1716;
      if (level_ < (jumptarget << 16)) level_ = jumptarget << 16;
      level_ += (((17 << 24) - level_) >> 24) * inc_;
      if (level_ >= targetlevel_) {
        level_ = targetlevel_;
        advance(ix_ + 1);
      }
    } else {
      level_ -= inc_;
      if (level_ <= targetlevel_) {
        level_ = targetlevel_;
        advance(ix_ + 1);
      }
    }
  }
  return level_;
}

void Env::keydown(bool d) {
  if (down_ != d) {
    down_ = d;
    advance(d ? 0 : 3);
  }
}

// Hold times, in 44.1 kHz samples, for a stage whose target equals the
// current level. The DX7 still spends time on such a stage; without this,
// envelopes like L1 == L2 run too fast. Rates 77..99 use 20 * (99 - rate).
static const int statics[77] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600,
    584325,  507150,  502740,  441000,  418950,  352800,  308700, 286650, 253575, 220500,
    220500,  176400,  145530,  145530,  125685,  110250,  110250, 88200,  88200,  74970,
    61740,   61740,   55125,   48510,   44100,   37485,   31311,  30870,  27562,  27562,
    22050,   18522,   17640,   15435,   14112,   13230,   11025,  9261,   9261,   7717,
    6615,    6615,    5512,    5512,    4410,    3969,    3969,   3439,   2866,   2690,
    2249,    1984,    1896,    1808,    1411,    1367,    1234,   1146,   926,    837,
    837,     705,     573,     573,     529,     441,     441};

void Env::advance(int newix) {
  ix_ = newix;
  if (ix_ >= 4) return;
  int newlevel = levels_[ix_];
  int actuallevel = scaleoutlevel(newlevel) >> 1;
  actuallevel = (actuallevel << 6) + outlevel_ - 4256;
  actuallevel = actuallevel < 16 ? 16 : actuallevel;
  targetlevel_ = actuallevel << 16;
  rising_ = targetlevel_ > level_;

  int qrate = (rates_[ix_] * 41) >> 6;
  qrate += rate_scaling_;
  qrate = std::min(qrate, 63);

  if (targetlevel_ == level_ || (ix_ == 0 && newlevel == 0)) {
    int staticrate = std::min(rates_[ix_] + rate_scaling_, 99);
    staticcount_ = staticrate < 77 ? statics[staticrate] : 20 * (99 - staticrate);
    if (staticrate < 77 && ix_ == 0 && newlevel == 0) staticcount_ /= 20;
    staticcount_ = (int32_t)(((int64_t)staticcount_ * Tables::srMultiplier) >> 24);
  } else {
    staticcount_ = 0;
  }
  inc_ = (4 + (qrate & 3)) << (2 + LG_N + (qrate >> 2));
  inc_ = (int32_t)(((int64_t)inc_ * Tables::srMultiplier) >> 24);
}

// ---------------------------------------------------------------- PitchEnv

int PitchEnv::unit_;

void PitchEnv::init(double sample_rate) {
  unit_ = (int)(N * (1 << 24) / (21.3 * sample_rate) + 0.5);
}

static const uint8_t pitchratetab[] = {
    1,   2,   3,   3,   4,   4,   5,   5,   6,   6,   7,   7,   8,   8,   9,   9,   10,
    10,  11,  11,  12,  12,  13,  13,  14,  14,  15,  16,  16,  17,  18,  18,  19,  20,
    21,  22,  23,  24,  25,  26,  27,  28,  30,  31,  33,  34,  36,  37,  38,  39,  41,
    42,  44,  46,  47,  49,  51,  53,  54,  56,  58,  60,  62,  64,  66,  68,  70,  72,
    74,  76,  79,  82,  85,  88,  91,  94,  98,  102, 106, 110, 115, 120, 125, 130, 135,
    141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};

static const int8_t pitchtab[] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35,
    -33,  -32,  -31,  -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17,
    -16,  -15,  -14,  -13, -12, -11, -10, -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,  0,
    1,    2,    3,    4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,
    18,   19,   20,   21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,
    35,   38,   40,   43,  46,  49,  53,  58,  65,  73,  82,  92,  103, 115, 127};

void PitchEnv::set(const int r[4], const int l[4]) {
  for (int i = 0; i < 4; i++) {
    rates_[i] = r[i];
    levels_[i] = l[i];
  }
  level_ = pitchtab[l[3]] << 19;
  down_ = true;
  advance(0);
}

void PitchEnv::update(const int r[4], const int l[4]) {
  for (int i = 0; i < 4; i++) {
    rates_[i] = r[i];
    levels_[i] = l[i];
  }
  if (ix_ < 4) advance(ix_);
}

int32_t PitchEnv::getsample() {
  if (ix_ < 3 || (ix_ < 4 && !down_)) {
    if (rising_) {
      level_ += inc_;
      if (level_ >= targetlevel_) {
        level_ = targetlevel_;
        advance(ix_ + 1);
      }
    } else {
      level_ -= inc_;
      if (level_ <= targetlevel_) {
        level_ = targetlevel_;
        advance(ix_ + 1);
      }
    }
  }
  return level_;
}

void PitchEnv::keydown(bool d) {
  if (down_ != d) {
    down_ = d;
    advance(d ? 0 : 3);
  }
}

void PitchEnv::advance(int newix) {
  ix_ = newix;
  if (ix_ < 4) {
    targetlevel_ = pitchtab[levels_[ix_]] << 19;
    rising_ = targetlevel_ > level_;
    inc_ = pitchratetab[rates_[ix_]] * unit_;
  }
}

// ---------------------------------------------------------------- Lfo

uint32_t Lfo::unit_;

void Lfo::init(double sample_rate) {
  // 1 << 32 / 15.5 s / 11
  unit_ = (uint32_t)(N * 25190424 / sample_rate + 0.5);
}

void Lfo::reset(const uint8_t params[6]) {
  int rate = params[0];
  int sr = rate == 0 ? 1 : (165 * rate) >> 6;
  sr *= sr < 160 ? 11 : (11 + ((sr - 160) >> 4));
  delta_ = unit_ * sr;
  int a = 99 - params[1];
  if (a == 99) {
    delayinc_ = ~0u;
    delayinc2_ = ~0u;
  } else {
    a = (16 + (a & 15)) << (1 + (a >> 4));
    delayinc_ = unit_ * a;
    a &= 0xff80;
    a = std::max(0x80, a);
    delayinc2_ = unit_ * a;
  }
  waveform_ = params[5];
  sync_ = params[4] != 0;
}

int32_t Lfo::getsample() {
  phase_ += delta_;
  int32_t x;
  switch (waveform_) {
    case 0:  // triangle
      x = (int32_t)(phase_ >> 7);
      x ^= -(int32_t)(phase_ >> 31);
      x &= (1 << 24) - 1;
      return x;
    case 1:  // saw down
      return (int32_t)((~phase_ ^ (1U << 31)) >> 8);
    case 2:  // saw up
      return (int32_t)((phase_ ^ (1U << 31)) >> 8);
    case 3:  // square
      return (int32_t)(((~phase_) >> 7) & (1 << 24));
    case 4:  // sine
      return (1 << 23) + (Sin::lookup((int32_t)(phase_ >> 8)) >> 1);
    case 5:  // sample and hold
      if (phase_ < delta_) randstate_ = (uint8_t)((randstate_ * 179 + 17) & 0xff);
      x = randstate_ ^ 0x80;
      return (x + 1) << 16;
  }
  return 1 << 23;
}

int32_t Lfo::getdelay() {
  uint32_t delta = delaystate_ < (1U << 31) ? delayinc_ : delayinc2_;
  uint64_t d = (uint64_t)delaystate_ + delta;
  if (d > ~0u) return 1 << 24;
  delaystate_ = (uint32_t)d;
  if (d < (1U << 31)) return 0;
  return (int32_t)((d >> 7) & ((1 << 24) - 1));
}

void Lfo::keydown() {
  if (sync_) phase_ = (1U << 31) - 1;
  delaystate_ = 0;
}

// ---------------------------------------------------------------- FmCore

namespace {
enum : int {
  OUT_BUS_ONE = 1 << 0,
  OUT_BUS_TWO = 1 << 1,
  OUT_BUS_ADD = 1 << 2,
  IN_BUS_ONE = 1 << 4,
  IN_BUS_TWO = 1 << 5,
  FB_IN = 1 << 6,
  FB_OUT = 1 << 7
};

// Routing per algorithm, ops in VCED order (OP6 first).
const int algorithms[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14}, {0x41, 0x11, 0x94, 0x01, 0x11, 0x14},
    {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0x41, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14},
    {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14},
    {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14},
    {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14},
    {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14},
    {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14}, {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04},
    {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14},
    {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04},
};

inline void opCompute(int32_t* out, const int32_t* in, uint32_t phase, uint32_t freq,
                      int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (N >> 1)) >> LG_N;
  int32_t gain = gain1;
  for (int i = 0; i < N; i++) {
    gain += dgain;
    int32_t y = Sin::lookup((int32_t)(phase + (uint32_t)in[i]));
    int32_t v = (int32_t)(((int64_t)y * gain) >> 24);
    out[i] = add ? out[i] + v : v;
    phase += freq;
  }
}

inline void opComputePure(int32_t* out, uint32_t phase, uint32_t freq, int32_t gain1,
                          int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (N >> 1)) >> LG_N;
  int32_t gain = gain1;
  for (int i = 0; i < N; i++) {
    gain += dgain;
    int32_t y = Sin::lookup((int32_t)phase);
    int32_t v = (int32_t)(((int64_t)y * gain) >> 24);
    out[i] = add ? out[i] + v : v;
    phase += freq;
  }
}

inline void opComputeFb(int32_t* out, uint32_t phase, uint32_t freq, int32_t gain1,
                        int32_t gain2, int32_t* fb_buf, int fb_shift, bool add) {
  int32_t dgain = (gain2 - gain1 + (N >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t y0 = fb_buf[0];
  int32_t y = fb_buf[1];
  for (int i = 0; i < N; i++) {
    gain += dgain;
    int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);
    y0 = y;
    y = Sin::lookup((int32_t)(phase + (uint32_t)scaled_fb));
    y = (int32_t)(((int64_t)y * gain) >> 24);
    out[i] = add ? out[i] + y : y;
    phase += freq;
  }
  fb_buf[0] = y0;
  fb_buf[1] = y;
}
}  // namespace

int FmCore::carrierMask(int algorithm) {
  int mask = 0;
  for (int i = 0; i < 6; i++)
    if ((algorithms[algorithm & 31][i] & 3) == 0)
      mask |= 1 << (5 - i);
  return mask;
}

int FmCore::carrierCount(int algorithm) {
  return __builtin_popcount(carrierMask(algorithm));
}

void FmCore::compute(int32_t* output, FmOpParams* params, int algorithm, int32_t* fb_buf,
                     int feedback_shift) {
  const int kLevelThresh = 1120;
  const int* alg = algorithms[algorithm & 31];
  bool has_contents[3] = {true, false, false};
  for (int op = 0; op < 6; op++) {
    int flags = alg[op];
    bool add = (flags & OUT_BUS_ADD) != 0;
    FmOpParams& param = params[op];
    int inbus = (flags >> 4) & 3;
    int outbus = flags & 3;
    int32_t* outptr = outbus == 0 ? output : buf_[outbus - 1];
    int32_t gain1 = param.gain[0];
    int32_t gain2 = param.gain[1];
    if (gain1 >= kLevelThresh || gain2 >= kLevelThresh) {
      if (!has_contents[outbus]) add = false;
      if (inbus == 0 || !has_contents[inbus]) {
        if ((flags & 0xc0) == 0xc0 && feedback_shift < 16) {
          opComputeFb(outptr, param.phase, param.freq, gain1, gain2, fb_buf, feedback_shift, add);
        } else {
          opComputePure(outptr, param.phase, param.freq, gain1, gain2, add);
        }
      } else {
        opCompute(outptr, buf_[inbus - 1], param.phase, param.freq, gain1, gain2, add);
      }
      has_contents[outbus] = true;
    } else if (!add) {
      has_contents[outbus] = false;
    }
    param.phase += (uint32_t)param.freq << LG_N;
  }
}

}  // namespace mf1
