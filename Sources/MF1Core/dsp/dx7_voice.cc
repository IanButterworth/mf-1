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

#include "dx7_voice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mf1 {

namespace {

int32_t midinoteToLogfreq(int midinote) {
  const int base = 50857777;  // (1 << 24) * (log2(440) - 69/12)
  const int step = (1 << 24) / 12;
  return base + step * midinote;
}

const int32_t coarsemul[] = {
    -16777216, 0,        16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
    50331648,  53182516, 55732705, 58039632, 60145690, 62083076, 63876816, 65546747,
    67108864,  68576247, 69959732, 71268397, 72509921, 73690858, 74816848, 75892776,
    76922906,  77910978, 78860292, 79773775, 80654032, 81503396, 82323963, 83117622};

int32_t oscFreq(int midinote, int mode, int coarse, int fine, int detune, bool quirks) {
  int32_t logfreq;
  if (mode == 0) {
    logfreq = midinoteToLogfreq(midinote);
    logfreq += coarsemul[coarse & 31];
    if (fine) logfreq += (int32_t)std::floor(24204406.323123 * std::log(1 + 0.01 * fine) + 0.5);
    // Measured at 7.213 Hz per count at 9600 Hz. FM-1 V15 detunes about
    // 2.8 cents per step instead of 0.96, a little wider going up.
    if (quirks)
      logfreq += (detune > 7 ? 40800 : 37600) * (detune - 7);
    else
      logfreq += 12606 * (detune - 7);
  } else {
    // Fixed frequency: 10^(coarse & 3) * 10^(fine / 100) Hz.
    logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
    logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
  }
  return logfreq;
}

const uint8_t velocityData[64] = {
    0,   70,  86,  97,  106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
    166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202, 205,
    206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232, 233, 235,
    237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};

int scaleVelocity(int velocity, int sensitivity) {
  int clamped = std::max(0, std::min(127, velocity));
  int vel_value = velocityData[clamped >> 1] - 239;
  return ((sensitivity * vel_value + 7) >> 3) << 4;
}

int scaleRate(int midinote, int sensitivity) {
  int x = std::min(31, std::max(0, midinote / 3 - 7));
  return (sensitivity * x) >> 3;
}

const uint8_t expScaleData[] = {0,  1,  2,  3,  4,  5,   6,   7,   8,   9,   11,
                                14, 16, 19, 23, 27, 33,  39,  47,  56,  66,  80,
                                94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};

int scaleCurve(int group, int depth, int curve) {
  int scale;
  if (curve == 0 || curve == 3) {
    scale = (group * depth * 329) >> 12;
  } else {
    int n = (int)sizeof(expScaleData);
    int raw = expScaleData[std::min(group, n - 1)];
    scale = (raw * depth * 329) >> 15;
  }
  if (curve < 2) scale = -scale;
  return scale;
}

int scaleLevel(int midinote, int bp, int ld, int rd, int lc, int rc) {
  int offset = midinote - bp - 17;
  if (offset >= 0) return scaleCurve(offset / 3, rd, rc);
  return scaleCurve((-offset) / 3, ld, lc);
}

const uint8_t pitchmodsenstab[] = {0, 10, 20, 33, 55, 92, 153, 255};
const uint8_t ampmodsenstab[] = {0, 66, 109, 255};
// Full depth tremolo per AMS step in log2 Q24. On the FM-1, AMS 1 and 2 give
// 2.1 and 5.3 dB peak to peak, and AMS 3 silences the operator at each trough.
const int32_t fm1AmpModDepth[] = {0, (int32_t)(0.349 * (1 << 24)), (int32_t)(0.880 * (1 << 24)),
                                  14 << 24};

}  // namespace

void Dx7Voice::setupPitch(const uint8_t* patch) {
  int note = midinote_ + (int)patch[144] - 24;
  for (int op = 0; op < 6; op++) {
    int off = op * 21;
    basepitch_[op] = oscFreq(note, patch[off + 17], patch[off + 18], patch[off + 19], patch[off + 20],
                             quirks_);
  }
}

void Dx7Voice::setupLevels(const uint8_t* patch, bool initial) {
  int note = midinote_ + (int)patch[144] - 24;
  for (int op = 0; op < 6; op++) {
    int off = op * 21;
    int rates[4], levels[4];
    for (int i = 0; i < 4; i++) {
      rates[i] = patch[off + i];
      levels[i] = patch[off + 4 + i];
    }
    int outlevel = Env::scaleoutlevel(patch[off + 16]);
    outlevel += scaleLevel(note, patch[off + 8], patch[off + 9], patch[off + 10], patch[off + 11],
                           patch[off + 12]);
    outlevel = std::min(127, outlevel);
    outlevel = outlevel << 5;
    outlevel += scaleVelocity(velocity_, patch[off + 15]);
    outlevel = std::max(0, outlevel);
    int rate_scaling = scaleRate(note, patch[off + 13]);
    if (initial)
      env_[op].init(rates, levels, outlevel, rate_scaling);
    else
      env_[op].update(rates, levels, outlevel, rate_scaling);
    // Attenuation at full LFO depth, log2 Q24. Plain msfa scales the DX7
    // table to about 6 doublings at AMS 3.
    ampmodsens_[op] = quirks_ ? fm1AmpModDepth[patch[off + 14] & 3]
                              : (int32_t)ampmodsenstab[patch[off + 14] & 3] * (6 << 16);
  }
}

void Dx7Voice::init(const uint8_t patch[kVoiceSize], int midinote, int velocity, bool keepPhase,
                    const Controllers& ctrls) {
  quirks_ = ctrls.fm1Quirks;
  midinote_ = midinote;
  velocity_ = velocity;
  setupLevels(patch, true);
  setupPitch(patch);
  bool oscSync = patch[136] != 0;
  for (int op = 0; op < 6; op++) {
    if (oscSync || !keepPhase) params_[op].phase = 0;
    params_[op].gain[1] = 0;
    params_[op].gain[0] = 0;
  }
  int rates[4], levels[4];
  for (int i = 0; i < 4; i++) {
    rates[i] = patch[126 + i];
    levels[i] = patch[130 + i];
  }
  pitchenv_.set(rates, levels);
  algorithm_ = patch[134];
  int feedback = patch[135];
  fb_shift_ = feedback != 0 ? 8 - feedback : 16;
  // FM-1 feedback is about a quarter of the msfa strength (-12 dB on H2).
  if (quirks_ && feedback != 0) fb_shift_ += 2;
  fb_buf_[0] = fb_buf_[1] = 0;
  pitchmoddepth_ = (patch[139] * 165) >> 6;
  ampmoddepth_ = (patch[140] * 165) >> 6;
  // Q8. FM-1 vibrato is about 0.895 of the DX7 depth, so keep the fraction.
  pitchmodsens_ = pitchmodsenstab[patch[143] & 7] * (quirks_ ? 229 : 256);
  opEnable_ = patch[kOpEnable] & 0x3f;
  carriers_ = std::max(1, FmCore::carrierCount(algorithm_));
  glideOffset_ = 0;
  glideStep_ = 0;
}

void Dx7Voice::update(const uint8_t patch[kVoiceSize]) {
  setupLevels(patch, false);
  setupPitch(patch);
  int rates[4], levels[4];
  for (int i = 0; i < 4; i++) {
    rates[i] = patch[126 + i];
    levels[i] = patch[130 + i];
  }
  pitchenv_.update(rates, levels);
  algorithm_ = patch[134];
  int feedback = patch[135];
  fb_shift_ = feedback != 0 ? 8 - feedback : 16;
  if (quirks_ && feedback != 0) fb_shift_ += 2;
  pitchmoddepth_ = (patch[139] * 165) >> 6;
  ampmoddepth_ = (patch[140] * 165) >> 6;
  pitchmodsens_ = pitchmodsenstab[patch[143] & 7] * (quirks_ ? 229 : 256);
  opEnable_ = patch[kOpEnable] & 0x3f;
  carriers_ = std::max(1, FmCore::carrierCount(algorithm_));
}

void Dx7Voice::transfer(int midinote) {
  midinote_ = midinote;
}

void Dx7Voice::setGlideFrom(int fromNote, double glideSeconds) {
  int32_t diff = midinoteToLogfreq(fromNote) - midinoteToLogfreq(midinote_) + glideOffset_;
  if (glideSeconds <= 0 || diff == 0) {
    glideOffset_ = 0;
    glideStep_ = 0;
    return;
  }
  double blocks = glideSeconds * Tables::sampleRate / N;
  glideOffset_ = diff;
  glideStep_ = (int32_t)std::max(1.0, std::abs((double)diff) / std::max(1.0, blocks));
}

void Dx7Voice::compute(int32_t* buf, int32_t lfo_val, int32_t lfo_delay, const Controllers& ctrls) {
  int32_t pitchmod = pitchenv_.getsample();

  // LFO pitch depth: patch PMD plus mod wheel and aftertouch when routed to pitch.
  int pmd = pitchmoddepth_;
  int amd = ampmoddepth_;
  int wheel = (ctrls.modWheel * ctrls.modWheelRange * 165) / (127 * 64);
  int at = (ctrls.aftertouch * ctrls.aftertouchRange * 165) / (127 * 64);
  if (ctrls.modWheelPitch) pmd = std::min(255, pmd + wheel);
  if (ctrls.aftertouchPitch) pmd = std::min(255, pmd + at);
  if (ctrls.modWheelAmp) amd = std::min(255, amd + wheel);
  if (ctrls.aftertouchAmp) amd = std::min(255, amd + at);

  int64_t pmdq = ((int64_t)pmd * lfo_delay) >> 8;  // Q24
  int64_t senslfo = (int64_t)pitchmodsens_ * (lfo_val - (1 << 23));
  pitchmod += (int32_t)((pmdq * senslfo) >> 39);

  int bendRange = ctrls.pitchBend >= 0x2000 ? ctrls.pitchBendUp : ctrls.pitchBendDown;
  int32_t pb = (int32_t)(((int64_t)(ctrls.pitchBend - 0x2000) * bendRange * ((1 << 24) / 12)) >> 13);
  pitchmod += pb + ctrls.masterTune;

  if (glideStep_) {
    if (glideOffset_ > 0) {
      glideOffset_ = std::max(0, glideOffset_ - glideStep_);
    } else {
      glideOffset_ = std::min(0, glideOffset_ + glideStep_);
    }
    if (glideOffset_ == 0) glideStep_ = 0;
    pitchmod += glideOffset_;
  }

  // Amplitude modulation: attenuation in log2 Q24, LFO scaled by depth and delay.
  int64_t ampmod = ((int64_t)amd * lfo_delay) >> 8;  // Q24, 0..~1
  ampmod = (ampmod * (int64_t)((1 << 24) - lfo_val)) >> 24;
  int egBias = 0;
  if (ctrls.modWheelEgBias) egBias += wheel;
  if (ctrls.aftertouchEgBias) egBias += at;

  for (int op = 0; op < 6; op++) {
    params_[op].gain[0] = params_[op].gain[1];
    int32_t level = env_[op].getsample();
    if (ampmodsens_[op]) {
      level -= (int32_t)((ampmod * ampmodsens_[op]) >> 24);
      if (egBias) level -= (int32_t)((((int64_t)egBias << 16) * ampmodsens_[op]) >> 24);
    }
    lastLevel_[op] = level;
    int32_t gain = level > 0 ? Exp2::lookup(level - (14 * (1 << 24))) : 0;
    if (!(opEnable_ & (1 << (5 - op)))) gain = 0;
    params_[op].freq = Freqlut::lookup(basepitch_[op] + pitchmod);
    params_[op].gain[1] = gain;
  }
  core_.compute(buf, params_, algorithm_, fb_buf_, fb_shift_);
}

void Dx7Voice::keyup() {
  for (int op = 0; op < 6; op++) env_[op].keydown(false);
  pitchenv_.keydown(false);
}

bool Dx7Voice::isFinished() const {
  int carriers = FmCore::carrierMask(algorithm_);
  for (int op = 0; op < 6; op++) {
    // Carrier bit for VCED index op is (5 - op).
    if (!(carriers & (1 << (5 - op)))) continue;
    if (!env_[op].isIdle()) return false;
    if (lastLevel_[op] > (16 << 16) + (1 << 20)) return false;
  }
  return true;
}

// ---------------------------------------------------------------- patch format

void unpackVmem(const uint8_t bulk[128], uint8_t patch[kVoiceSize]) {
  for (int op = 0; op < 6; op++) {
    memcpy(patch + op * 21, bulk + op * 17, 11);
    uint8_t curves = bulk[op * 17 + 11];
    patch[op * 21 + 11] = curves & 3;
    patch[op * 21 + 12] = (curves >> 2) & 3;
    uint8_t detune_rs = bulk[op * 17 + 12];
    patch[op * 21 + 13] = detune_rs & 7;
    patch[op * 21 + 20] = (detune_rs >> 3) & 15;
    uint8_t kvs_ams = bulk[op * 17 + 13];
    patch[op * 21 + 14] = kvs_ams & 3;
    patch[op * 21 + 15] = (kvs_ams >> 2) & 7;
    patch[op * 21 + 16] = bulk[op * 17 + 14];
    uint8_t coarse_mode = bulk[op * 17 + 15];
    patch[op * 21 + 17] = coarse_mode & 1;
    patch[op * 21 + 18] = (coarse_mode >> 1) & 31;
    patch[op * 21 + 19] = bulk[op * 17 + 16];
  }
  memcpy(patch + 126, bulk + 102, 9);
  uint8_t oks_fb = bulk[111];
  patch[135] = oks_fb & 7;
  patch[136] = (oks_fb >> 3) & 1;
  memcpy(patch + 137, bulk + 112, 4);
  uint8_t lpms_lfw_lks = bulk[116];
  patch[141] = lpms_lfw_lks & 1;
  patch[142] = (lpms_lfw_lks >> 1) & 7;
  patch[143] = (lpms_lfw_lks >> 4) & 7;
  memcpy(patch + 144, bulk + 117, 11);
  patch[kOpEnable] = 0x3f;
  sanitizeVoice(patch);
}

void packVmem(const uint8_t patch[kVoiceSize], uint8_t bulk[128]) {
  for (int op = 0; op < 6; op++) {
    const uint8_t* p = patch + op * 21;
    uint8_t* b = bulk + op * 17;
    memcpy(b, p, 11);
    b[11] = (uint8_t)((p[11] & 3) | ((p[12] & 3) << 2));
    b[12] = (uint8_t)((p[13] & 7) | ((p[20] & 15) << 3));
    b[13] = (uint8_t)((p[14] & 3) | ((p[15] & 7) << 2));
    b[14] = p[16];
    b[15] = (uint8_t)((p[17] & 1) | ((p[18] & 31) << 1));
    b[16] = p[19];
  }
  memcpy(bulk + 102, patch + 126, 9);
  bulk[111] = (uint8_t)((patch[135] & 7) | ((patch[136] & 1) << 3));
  memcpy(bulk + 112, patch + 137, 4);
  bulk[116] = (uint8_t)((patch[141] & 1) | ((patch[142] & 7) << 1) | ((patch[143] & 7) << 4));
  memcpy(bulk + 117, patch + 144, 11);
  for (int i = 0; i < 128; i++) bulk[i] &= 0x7f;
}

int vcedMax(int index) {
  if (index < 126) {
    static const uint8_t opMax[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                      3,  3,  7,  3,  7,  99, 1,  31, 99, 14};
    return opMax[index % 21];
  }
  static const uint8_t globalMax[] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
  if (index < 145) return globalMax[index - 126];
  if (index < 155) return 127;
  if (index == kOpEnable) return 63;
  return 0;
}

void sanitizeVoice(uint8_t patch[kVoiceSize]) {
  for (int i = 0; i < kVoiceSize; i++) {
    int m = vcedMax(i);
    if (patch[i] > m) patch[i] = (uint8_t)m;
  }
  for (int i = 145; i < 155; i++)
    if (patch[i] < 32) patch[i] = 32;
}

// The DX7 INIT VOICE: algorithm 1, only OP1 sounding at full level.
const uint8_t kInitVoice[kVoiceSize] = {
    // OP6..OP2
    99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7,
    99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7,
    99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7,
    99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7,
    99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7,
    // OP1
    99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 99, 0, 1, 0, 7,
    // pitch EG rates and levels, algorithm, feedback, osc key sync
    99, 99, 99, 99, 50, 50, 50, 50, 0, 0, 1,
    // LFO speed, delay, PMD, AMD, sync, wave, PMS, transpose
    35, 0, 0, 0, 1, 0, 3, 24,
    'I', 'N', 'I', 'T', ' ', 'V', 'O', 'I', 'C', 'E',
    0x3f,
};

}  // namespace mf1
