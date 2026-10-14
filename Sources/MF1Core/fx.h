#pragma once
#include <cstdint>
#include <vector>

namespace mf1 {

enum FxSlot { kFxFilter, kFxReverb, kFxDelay, kFxDistortion, kFxChorus, kFxPhaser, kFxCount };

// The FM-1 effect section: six effects in a reorderable mono chain.
// get/set use the published FX CC numbering and ranges (switches 0/1,
// filter type 0..2, cutoff 0..107, Q 0..10, everything else 0..100).
// The panel shows Q as 0..100, so it is stored at that resolution.
struct FxParams {
  int order[kFxCount] = {kFxFilter, kFxReverb, kFxDelay, kFxDistortion, kFxChorus, kFxPhaser};
  bool filterOn = false;
  int filterType = 0;  // 0 LPF, 1 BPF, 2 HPF
  int filterCutoff = 107;
  int filterQ = 0;  // 0..100
  bool reverbOn = false;
  int reverbType = 0;  // 0 Room, 1 Hall, 2 Plate
  int reverbDecay = 50;
  int reverbMix = 30;
  bool delayOn = false;
  int delayDecay = 40;
  int delayRate = 40;
  int delayMix = 30;
  bool distOn = false;
  int distGain = 30;
  int distTone = 50;
  int distLevel = 50;
  bool chorusOn = false;
  int chorusFreq = 30;
  int chorusDepth = 50;
  int chorusMix = 50;
  bool phaserOn = false;
  int phaserFreq = 30;
  int phaserDepth = 50;
  int phaserMix = 50;

  // Index order matches the FX CC numbers 0..23.
  int get(int cc) const;
  void set(int cc, int value);
  static int maxValue(int cc);
  // Cutoff step 0..107 to Hz, 100 Hz to 20 kHz on a log scale.
  static double cutoffHz(int step);
  bool slotOn(int slot) const;
};

class FxChain {
 public:
  void prepare(double sampleRate);
  void setParams(const FxParams& p);
  // In place, mono.
  void process(float* x, int frames);

 private:
  struct Smoothed {
    float value = 0, target = 0;
    float next(float coef) { return value += (target - value) * coef; }
  };
  double sr_ = 48000;
  FxParams p_;
  float smoothCoef_ = 0.001f;

  // Filter: topology preserving state variable filter per channel.
  float svfIc1_ = 0, svfIc2_ = 0;
  Smoothed cutoff_, q_;
  // Cutoff and Q the coefficients were last computed for.
  float filterC_ = -1, filterQ_ = -1;
  float filterK_ = 0, filterA1_ = 0, filterA2_ = 0, filterA3_ = 0;
  // Distortion
  float toneState_ = 0;
  // Chorus
  std::vector<float> chorusBuf_;
  int chorusPos_ = 0;
  double chorusPhase_ = 0;
  float chorusDelay_[2] = {};
  bool chorusReady_ = false;
  // Phaser
  float apState_[6] = {};
  float phaserFb_ = 0;
  double phaserPhase_ = 0;
  float phaserCoef_ = 0;
  bool phaserReady_ = false;
  // Delay
  std::vector<float> delayBuf_;
  int delayPos_ = 0;
  Smoothed delayTime_;
  float delayDamp_ = 0;
  float delayDampCoef_ = 0.35f;
  // Reverb: 8 line feedback delay network with input diffusion.
  static constexpr int kLines = 8;
  std::vector<float> fdn_[kLines];
  int fdnLen_[kLines] = {};
  int fdnPos_[kLines] = {};
  float fdnDamp_[kLines] = {};
  std::vector<float> diff_[4];
  int diffLen_[4] = {};
  int diffPos_[4] = {};
  std::vector<float> predelay_;
  int predelayPos_ = 0;
  int predelayLen_ = 0;
  float fdnGain_[kLines] = {};
  float reverbDampCoef_ = 0.7f;
  int reverbTypeBuilt_ = -1;
  void buildReverb(int type);
  void runFilter(float* x, int n);
  void runDistortion(float* x, int n);
  void runChorus(float* x, int n);
  void runPhaser(float* x, int n);
  void runDelay(float* x, int n);
  void runReverb(float* x, int n);
  void updateReverbGains();
};

}  // namespace mf1
