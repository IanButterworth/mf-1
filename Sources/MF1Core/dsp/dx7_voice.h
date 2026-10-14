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

#pragma once
#include <cstdint>

#include "fm_dsp.h"

namespace mf1 {

// Unpacked DX7 voice (VCED order, 155 bytes) plus an operator enable byte.
constexpr int kVoiceSize = 156;
constexpr int kOpEnable = 155;

struct Controllers {
  int32_t pitchBend = 0x2000;  // 14 bit
  int pitchBendUp = 2;         // semitones
  int pitchBendDown = 2;
  int modWheel = 0;            // 0..127
  int modWheelRange = 99;      // 0..99
  bool modWheelPitch = true;
  bool modWheelAmp = false;
  bool modWheelEgBias = false;
  int aftertouch = 0;
  int aftertouchRange = 0;
  bool aftertouchPitch = false;
  bool aftertouchAmp = false;
  bool aftertouchEgBias = false;
  // Extra log2 Q24 pitch shared by all voices, e.g. master tune.
  int32_t masterTune = 0;
  // Reproduce the measured deviations of FM-1 firmware V15 from msfa:
  // wider detune, weaker feedback, slower pitch EG, LFO depth tables.
  bool fm1Quirks = true;
};

class Dx7Voice {
 public:
  void init(const uint8_t patch[kVoiceSize], int midinote, int velocity, bool keepPhase,
            const Controllers& ctrls);
  // Re-reads the patch while the note sounds, so edits are heard at once.
  void update(const uint8_t patch[kVoiceSize]);
  // Adds one block of N samples into buf.
  void compute(int32_t* buf, int32_t lfo_val, int32_t lfo_delay, const Controllers& ctrls);
  void keyup();
  // Legato retrigger for mono mode: new pitch, envelopes keep running.
  void transfer(int midinote);
  bool isFinished() const;
  int midinote() const { return midinote_; }
  // Glide from another pitch to this note, over a time in samples.
  void setGlideFrom(int fromNote, double glideSeconds);
  int32_t currentLevel(int op) const { return lastLevel_[op]; }

 private:
  void setupPitch(const uint8_t* patch);
  bool quirks_ = false;
  void setupLevels(const uint8_t* patch, bool initial);

  FmCore core_;
  Env env_[6];
  FmOpParams params_[6];
  PitchEnv pitchenv_;
  int32_t basepitch_[6] = {};
  int32_t fb_buf_[2] = {};
  int fb_shift_ = 16;
  int algorithm_ = 0;
  int pitchmoddepth_ = 0;
  int pitchmodsens_ = 0;
  int ampmoddepth_ = 0;
  int32_t ampmodsens_[6] = {};
  int opEnable_ = 0x3f;
  int carriers_ = 1;
  int midinote_ = 60;
  int velocity_ = 100;
  int32_t lastLevel_[6] = {};
  // Portamento, as a log2 Q24 offset that decays to zero.
  int32_t glideOffset_ = 0;
  int32_t glideStep_ = 0;
};

// Pack/unpack between the 128 byte VMEM voice and the 155 byte VCED voice.
void unpackVmem(const uint8_t bulk[128], uint8_t patch[kVoiceSize]);
void packVmem(const uint8_t patch[kVoiceSize], uint8_t bulk[128]);
// Clamps every VCED field to its legal range.
void sanitizeVoice(uint8_t patch[kVoiceSize]);
int vcedMax(int index);
extern const uint8_t kInitVoice[kVoiceSize];

}  // namespace mf1
