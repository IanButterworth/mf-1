#include "fx.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mf1 {

namespace {
constexpr float kPi = 3.14159265358979f;
// The LFOs and filter coefficients are updated once per this many samples.
constexpr int kCtlBlock = 16;

// Base lengths in ms for each reverb type; scaled by the sample rate.
struct ReverbShape {
  float lines[8];
  float diffusers[4];
  float predelayMs;
  float dampHz;   // cutoff of the one pole lowpass inside the loop
  float rtScale;  // RT40 relative to Hall at the same decay
};

const ReverbShape kShapes[3] = {
    // Room: short and dense.
    {{11.3f, 13.7f, 15.1f, 17.9f, 19.3f, 21.7f, 23.9f, 26.1f}, {1.7f, 2.9f, 4.1f, 5.3f}, 4, 20000, 0.74f},
    // Hall: long lines, more predelay.
    {{37.1f, 41.3f, 47.9f, 53.1f, 59.3f, 63.7f, 71.9f, 79.3f}, {3.1f, 4.7f, 7.3f, 9.7f}, 18, 17000, 1.f},
    // Plate: no predelay, bright, very diffuse.
    {{17.3f, 19.9f, 23.3f, 27.1f, 29.9f, 33.7f, 37.3f, 41.9f}, {1.1f, 1.9f, 2.7f, 3.9f}, 0, 20000, 0.86f},
};

// Hall RT40 in seconds at decay 0, 5, 25, 50, 75 and 100, as measured on the FM-1.
const float kDecayPoints[6] = {0, 5, 25, 50, 75, 100};
const float kHallRt40[6] = {0.12f, 0.40f, 1.5f, 2.3f, 3.7f, 5.0f};

float hallRt40(int decay) {
  for (int i = 1; i < 6; i++) {
    if (decay <= kDecayPoints[i]) {
      float t = (decay - kDecayPoints[i - 1]) / (kDecayPoints[i] - kDecayPoints[i - 1]);
      return kHallRt40[i - 1] + t * (kHallRt40[i] - kHallRt40[i - 1]);
    }
  }
  return kHallRt40[5];
}

// The manual gives 0.8 s down to 0.1 s but not the curve, so take the plain linear map.
double delaySeconds(int rate) { return 0.8 - 0.7 * rate / 100.0; }

float onePoleCoef(double hz, double sr) { return (float)(1.0 - std::exp(-2.0 * M_PI * hz / sr)); }

// Pade approximant of tanh: close enough for a soft clipper and much cheaper.
inline float fastTanh(float x) {
  x = std::max(-4.97f, std::min(4.97f, x));
  float x2 = x * x;
  float num = x * (135135.f + x2 * (17325.f + x2 * (378.f + x2)));
  float den = 135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f));
  return num / den;
}
}  // namespace

int FxParams::get(int cc) const {
  switch (cc) {
    case 0: return filterOn;
    case 1: return filterType;
    case 2: return filterCutoff;
    case 3: return (filterQ + 5) / 10;
    case 4: return reverbOn;
    case 5: return reverbType;
    case 6: return reverbDecay;
    case 7: return reverbMix;
    case 8: return delayOn;
    case 9: return delayDecay;
    case 10: return delayRate;
    case 11: return delayMix;
    case 12: return distOn;
    case 13: return distGain;
    case 14: return distTone;
    case 15: return distLevel;
    case 16: return chorusOn;
    case 17: return chorusFreq;
    case 18: return chorusDepth;
    case 19: return chorusMix;
    case 20: return phaserOn;
    case 21: return phaserFreq;
    case 22: return phaserDepth;
    case 23: return phaserMix;
  }
  return 0;
}

int FxParams::maxValue(int cc) {
  switch (cc) {
    case 0: case 4: case 8: case 12: case 16: case 20: return 1;
    case 1: case 5: return 2;
    case 2: return 107;
    case 3: return 10;
  }
  return cc < 24 ? 100 : 0;
}

void FxParams::set(int cc, int value) {
  int m = maxValue(cc);
  // Switches treat any value of 1 or more as on.
  if (m == 1) value = value >= 1 ? 1 : 0;
  value = std::max(0, std::min(m, value));
  switch (cc) {
    case 0: filterOn = value; break;
    case 1: filterType = value; break;
    case 2: filterCutoff = value; break;
    case 3: filterQ = value * 10; break;
    case 4: reverbOn = value; break;
    case 5: reverbType = value; break;
    case 6: reverbDecay = value; break;
    case 7: reverbMix = value; break;
    case 8: delayOn = value; break;
    case 9: delayDecay = value; break;
    case 10: delayRate = value; break;
    case 11: delayMix = value; break;
    case 12: distOn = value; break;
    case 13: distGain = value; break;
    case 14: distTone = value; break;
    case 15: distLevel = value; break;
    case 16: chorusOn = value; break;
    case 17: chorusFreq = value; break;
    case 18: chorusDepth = value; break;
    case 19: chorusMix = value; break;
    case 20: phaserOn = value; break;
    case 21: phaserFreq = value; break;
    case 22: phaserDepth = value; break;
    case 23: phaserMix = value; break;
  }
}

double FxParams::cutoffHz(int step) {
  return 100.0 * std::pow(200.0, std::max(0, std::min(107, step)) / 107.0);
}

bool FxParams::slotOn(int slot) const {
  switch (slot) {
    case kFxFilter: return filterOn;
    case kFxReverb: return reverbOn;
    case kFxDelay: return delayOn;
    case kFxDistortion: return distOn;
    case kFxChorus: return chorusOn;
    case kFxPhaser: return phaserOn;
  }
  return false;
}

void FxChain::prepare(double sampleRate) {
  sr_ = sampleRate;
  smoothCoef_ = (float)(1.0 - std::exp(-1.0 / (0.02 * sampleRate)));
  chorusBuf_.assign((size_t)(0.05 * sampleRate) + 4, 0.f);
  delayBuf_.assign((size_t)(2.0 * sampleRate) + 4, 0.f);
  float scale = (float)(sampleRate / 1000.0);
  for (int i = 0; i < kLines; i++) {
    float maxMs = 0;
    for (auto& s : kShapes) maxMs = std::max(maxMs, s.lines[i]);
    fdn_[i].assign((size_t)(maxMs * scale) + 8, 0.f);
  }
  for (int i = 0; i < 4; i++) {
    float maxMs = 0;
    for (auto& s : kShapes) maxMs = std::max(maxMs, s.diffusers[i]);
    diff_[i].assign((size_t)(maxMs * scale) + 8, 0.f);
  }
  predelay_.assign((size_t)(0.05 * sampleRate) + 8, 0.f);
  reverbTypeBuilt_ = -1;
  buildReverb(p_.reverbType);
  cutoff_.value = cutoff_.target = p_.filterCutoff;
  q_.value = q_.target = p_.filterQ;
  delayTime_.value = delayTime_.target = (float)(delaySeconds(p_.delayRate) * sr_);
  // Each echo loses treble; 3.3 kHz matches the original tuning at 48 kHz.
  delayDampCoef_ = onePoleCoef(3300, sampleRate);
  filterC_ = -1;
  chorusReady_ = phaserReady_ = false;
}

void FxChain::buildReverb(int type) {
  const ReverbShape& s = kShapes[type];
  float scale = (float)(sr_ / 1000.0);
  for (int i = 0; i < kLines; i++) {
    fdnLen_[i] = std::max(8, (int)(s.lines[i] * scale));
    fdnPos_[i] = 0;
    fdnDamp_[i] = 0;
    std::fill(fdn_[i].begin(), fdn_[i].end(), 0.f);
  }
  for (int i = 0; i < 4; i++) {
    diffLen_[i] = std::max(4, (int)(s.diffusers[i] * scale));
    diffPos_[i] = 0;
    std::fill(diff_[i].begin(), diff_[i].end(), 0.f);
  }
  predelayLen_ = std::max(1, (int)(s.predelayMs * scale));
  predelayPos_ = 0;
  std::fill(predelay_.begin(), predelay_.end(), 0.f);
  reverbDampCoef_ = onePoleCoef(s.dampHz, sr_);
  reverbTypeBuilt_ = type;
  updateReverbGains();
}

void FxChain::updateReverbGains() {
  const ReverbShape& s = kShapes[std::max(0, reverbTypeBuilt_)];
  float rt40 = hallRt40(p_.reverbDecay) * s.rtScale;
  // The loop lowpass takes some level on every pass. Give it back at 1 kHz,
  // so the midrange of the tail decays at the measured rate.
  double a = reverbDampCoef_;
  double w = 2 * M_PI * 1000.0 / sr_;
  double dampGain = a / std::sqrt(1 - 2 * (1 - a) * std::cos(w) + (1 - a) * (1 - a));
  for (int i = 0; i < kLines; i++) {
    float seconds = fdnLen_[i] / (float)sr_;
    float g = std::pow(10.f, -2.f * seconds / rt40) / (float)dampGain;
    fdnGain_[i] = std::min(g, 0.999f);
  }
}

void FxChain::setParams(const FxParams& p) {
  bool decayChanged = p.reverbDecay != p_.reverbDecay;
  p_ = p;
  if (p_.reverbType != reverbTypeBuilt_) {
    buildReverb(p_.reverbType);
  } else if (decayChanged) {
    updateReverbGains();
  }
  cutoff_.target = (float)p_.filterCutoff;
  q_.target = (float)p_.filterQ;
  delayTime_.target = (float)(delaySeconds(p_.delayRate) * sr_);
}

void FxChain::process(float* x, int frames) {
  for (int i = 0; i < kFxCount; i++) {
    int slot = p_.order[i];
    if (!p_.slotOn(slot)) continue;
    switch (slot) {
      case kFxFilter: runFilter(x, frames); break;
      case kFxReverb: runReverb(x, frames); break;
      case kFxDelay: runDelay(x, frames); break;
      case kFxDistortion: runDistortion(x, frames); break;
      case kFxChorus: runChorus(x, frames); break;
      case kFxPhaser: runPhaser(x, frames); break;
    }
  }
  if (!p_.filterOn) {
    cutoff_.value = cutoff_.target;
    q_.value = q_.target;
  }
  if (!p_.delayOn) delayTime_.value = delayTime_.target;
}

void FxChain::runFilter(float* x, int n) {
  const float coef = smoothCoef_;
  for (int start = 0; start < n; start += kCtlBlock) {
    int end = std::min(n, start + kCtlBlock);
    for (int i = start; i < end; i++) {
      cutoff_.next(coef);
      q_.next(coef);
    }
    // Snap when close, so the coefficients stop being recomputed.
    if (std::fabs(cutoff_.target - cutoff_.value) < 1e-3f) cutoff_.value = cutoff_.target;
    if (std::fabs(q_.target - q_.value) < 1e-3f) q_.value = q_.target;
    if (cutoff_.value != filterC_ || q_.value != filterQ_) {
      filterC_ = cutoff_.value;
      filterQ_ = q_.value;
      float hz = 100.f * std::pow(200.f, filterC_ / 107.f);
      hz = std::min(hz, (float)sr_ * 0.45f);
      float g = std::tan(kPi * hz / (float)sr_);
      // Damping: Q 0 is a gentle Butterworth-like slope, Q 100 rings hard.
      filterK_ = 1.414f - 1.35f * (filterQ_ / 100.f);
      filterA1_ = 1.f / (1.f + g * (g + filterK_));
      filterA2_ = g * filterA1_;
      filterA3_ = g * filterA2_;
    }
    const float k = filterK_, a1 = filterA1_, a2 = filterA2_, a3 = filterA3_;
    for (int i = start; i < end; i++) {
      float v0 = x[i];
      float v3 = v0 - svfIc2_;
      float v1 = a1 * svfIc1_ + a2 * v3;
      float v2 = svfIc2_ + a2 * svfIc1_ + a3 * v3;
      svfIc1_ = 2 * v1 - svfIc1_;
      svfIc2_ = 2 * v2 - svfIc2_;
      x[i] = p_.filterType == 0 ? v2 : p_.filterType == 1 ? v1 * k : v0 - k * v1 - v2;
    }
  }
}

void FxChain::runDistortion(float* x, int n) {
  float g2 = (p_.distGain / 100.f) * (p_.distGain / 100.f);
  float drive = 1.f + g2 * 60.f;
  float toneHz = 800.f * std::pow(2.f, p_.distTone / 100.f * 4.5f);
  float toneA = 1.f - std::exp(-2.f * kPi * toneHz / (float)sr_);
  // Quiet signals pass at about unity at any gain. Hard clipping loses
  // loudness, so high gain gets up to 9.5 dB of makeup.
  float out = (1.f + 2.f * g2) / drive;
  // A plain linear trim: level 20 is about -14 dB, as measured.
  float level = p_.distLevel / 100.f;
  float s = toneState_;
  for (int i = 0; i < n; i++) {
    float y = fastTanh(x[i] * drive) * out;
    s += toneA * (y - s);
    x[i] = s * level;
  }
  toneState_ = s;
}

void FxChain::runChorus(float* x, int n) {
  // 0.1 to 1 Hz, per the FM-1+VA manual.
  double rate = 0.1 * std::pow(10.0, p_.chorusFreq / 100.0);
  double inc = rate / sr_;
  float depth = (float)(0.0005 + p_.chorusDepth / 100.0 * 0.006) * (float)sr_;
  float base = 0.008f * (float)sr_;
  float mix = p_.chorusMix / 100.f;
  int len = (int)chorusBuf_.size();
  // Two taps a quarter cycle apart thicken the mono signal.
  auto tapDelay = [&](int t) {
    return base + depth * 0.5f * (1.f + (float)std::sin(2 * M_PI * (chorusPhase_ + t * 0.25)));
  };
  if (!chorusReady_) {
    chorusDelay_[0] = tapDelay(0);
    chorusDelay_[1] = tapDelay(1);
    chorusReady_ = true;
  }
  for (int start = 0; start < n; start += kCtlBlock) {
    int end = std::min(n, start + kCtlBlock);
    chorusPhase_ += inc * (end - start);
    if (chorusPhase_ >= 1) chorusPhase_ -= 1;
    // The LFO is slow, so the delay can move in straight lines between control points.
    float d[2], step[2];
    for (int t = 0; t < 2; t++) {
      d[t] = chorusDelay_[t];
      chorusDelay_[t] = tapDelay(t);
      step[t] = (chorusDelay_[t] - d[t]) / (end - start);
    }
    for (int i = start; i < end; i++) {
      chorusBuf_[chorusPos_] = x[i];
      float wet = 0;
      for (int t = 0; t < 2; t++) {
        d[t] += step[t];
        float rp = chorusPos_ - d[t];
        while (rp < 0) rp += len;
        int i0 = (int)rp;
        float frac = rp - i0;
        int i1 = (i0 + 1) % len;
        wet += 0.5f * (chorusBuf_[i0] * (1 - frac) + chorusBuf_[i1] * frac);
      }
      x[i] = x[i] * (1 - mix * 0.5f) + wet * mix;
      chorusPos_ = (chorusPos_ + 1) % len;
    }
  }
}

void FxChain::runPhaser(float* x, int n) {
  // 0.5 to 6 Hz, per the FM-1+VA manual.
  double rate = 0.5 * std::pow(12.0, p_.phaserFreq / 100.0);
  double inc = rate / sr_;
  float depth = p_.phaserDepth / 100.f;
  float mix = p_.phaserMix / 100.f;
  auto allpassCoef = [&]() {
    double lfo = 0.5 + 0.5 * std::sin(2 * M_PI * phaserPhase_);
    float hz = 200.f * std::pow(2.f, (float)lfo * depth * 5.f);
    float t = std::tan(kPi * hz / (float)sr_);
    return (t - 1.f) / (t + 1.f);
  };
  if (!phaserReady_) {
    phaserCoef_ = allpassCoef();
    phaserReady_ = true;
  }
  for (int start = 0; start < n; start += kCtlBlock) {
    int end = std::min(n, start + kCtlBlock);
    phaserPhase_ += inc * (end - start);
    if (phaserPhase_ >= 1) phaserPhase_ -= 1;
    float a = phaserCoef_;
    phaserCoef_ = allpassCoef();
    float step = (phaserCoef_ - a) / (end - start);
    for (int i = start; i < end; i++) {
      a += step;
      float v = x[i] + phaserFb_ * 0.5f;
      for (int s = 0; s < 6; s++) {
        float y = a * v + apState_[s];
        apState_[s] = v - a * y;
        v = y;
      }
      phaserFb_ = v;
      x[i] = x[i] * (1 - mix * 0.5f) + v * mix * 0.7f;
    }
  }
}

void FxChain::runDelay(float* x, int n) {
  float fb = (p_.delayDecay / 100.f) * 0.92f;
  float mix = p_.delayMix / 100.f;
  int len = (int)delayBuf_.size();
  for (int i = 0; i < n; i++) {
    float d = delayTime_.next(smoothCoef_ * 0.2f);
    float rp = delayPos_ - d;
    while (rp < 0) rp += len;
    int i0 = (int)rp;
    float frac = rp - i0;
    int i1 = (i0 + 1) % len;
    float tap = delayBuf_[i0] * (1 - frac) + delayBuf_[i1] * frac;
    delayDamp_ += delayDampCoef_ * (tap - delayDamp_);
    delayBuf_[delayPos_] = x[i] + delayDamp_ * fb;
    x[i] += tap * mix;
    delayPos_ = (delayPos_ + 1) % len;
  }
}

void FxChain::runReverb(float* x, int n) {
  float mix = p_.reverbMix / 100.f;
  float damp = reverbDampCoef_;
  int plen = (int)predelay_.size();
  for (int i = 0; i < n; i++) {
    predelay_[predelayPos_] = x[i];
    int rp = predelayPos_ - predelayLen_;
    if (rp < 0) rp += plen;
    float v = predelay_[rp];
    predelayPos_ = (predelayPos_ + 1) % plen;
    for (int d = 0; d < 4; d++) {
      float delayed = diff_[d][diffPos_[d]];
      float w = v + 0.6f * delayed;
      diff_[d][diffPos_[d]] = w;
      v = delayed - 0.6f * w;
      if (++diffPos_[d] >= diffLen_[d]) diffPos_[d] = 0;
    }
    float outs[kLines];
    float sum = 0;
    for (int k = 0; k < kLines; k++) {
      float o = fdn_[k][fdnPos_[k]];
      fdnDamp_[k] += damp * (o - fdnDamp_[k]);
      outs[k] = fdnDamp_[k] * fdnGain_[k];
      sum += outs[k];
    }
    // Householder feedback matrix keeps the network lossless before the gains.
    float h = sum * (2.f / kLines);
    float wet = 0;
    for (int k = 0; k < kLines; k++) {
      fdn_[k][fdnPos_[k]] = outs[k] - h + v;
      if (++fdnPos_[k] >= fdnLen_[k]) fdnPos_[k] = 0;
      wet += (k & 1) ? -outs[k] : outs[k];
    }
    x[i] = x[i] * (1 - mix * 0.5f) + wet * 0.25f * mix;
  }
}

}  // namespace mf1
