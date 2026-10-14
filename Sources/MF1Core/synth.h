#pragma once
#include <atomic>
#include <cstdint>

#include "dsp/dx7_voice.h"
#include "fx.h"
#include "spsc_queue.h"

namespace mf1 {

constexpr int kNumPresets = 128;
constexpr int kMaxPolyphony = 12;
constexpr int kSeqPatterns = 16;
constexpr int kSeqSteps = 16;
constexpr int kStepNotes = 6;

// One preset: the DX7 voice plus the FM-1 settings stored with it.
struct Preset {
  uint8_t voice[kVoiceSize];
  FxParams fx;
  bool mono = false;
  bool envOn = false;
  uint8_t env[4] = {0, 50, 100, 20};  // attack, decay, sustain, release, 0..100
};

// Arp and sequencer rates, in whole notes.
constexpr int kNumRates = 10;
constexpr int kDefaultRate = 6;  // 1/16
extern const char* const kRateNames[kNumRates];
extern const double kRateWholeNotes[kNumRates];

enum ArpMode { kArpUp, kArpDown, kArpInclusive, kArpExclusive, kArpRandom, kArpOrder, kArpRepeat, kArpModeCount };
extern const char* const kArpModeNames[kArpModeCount];

// Global and not saved on the hardware.
struct ArpSettings {
  int mode = kArpUp;
  int octaves = 1;  // 1..4
  int rate = kDefaultRate;
  int gate = 50;    // 20..100 %
  int swing = 50;   // 50..75 %
  bool latch = false;
  bool sync = false;
};

struct SeqStep {
  uint8_t notes[kStepNotes];  // 0 means empty
  uint8_t velocity[kStepNotes];
  uint8_t length;  // steps this note group lasts; long notes tie over later steps
  uint8_t rate;    // the rate is stored per step
};

struct SeqPattern {
  SeqStep steps[kSeqSteps];
  int voice = 0;      // 0..127, the preset bound to this pattern
  int rate = kDefaultRate;
  int gate = 50;
  int swing = 50;
  bool sync = false;
  int transpose = 0;  // -24..24
  int length = 16;    // 1..16 steps
  int chain = 1;      // 1..16 patterns played in turn from this one
  SeqPattern();
  bool isEmpty() const;
};

enum GlideMode { kGlideFullTime, kGlideFingered };

struct GlobalSettings {
  int midiChannel = 0;   // 0 = all, else 1..16
  int fxChannel = 2;     // 1..16
  int bendUp = 12;       // 1..24
  int bendDown = 12;     // 1..24
  int keyVelocity = 90;  // velocity the on-board keys send, 1..127
  int glideMode = kGlideFullTime;
  int glideTime = 0;     // 0..100 = 0..1 s
  int masterVolume = 80; // 0..100, the MASTER pot
  int tempo = 120;       // 30..300, shared by arp and seq
  // Not a hardware setting: reproduce the measured V15 deviations, the
  // output filters and 16 bit output. Off gives a clean msfa engine.
  bool fm1Quirks = true;
};

struct Command {
  enum Type : uint8_t {
    NoteOn, NoteOff, LoadPreset, SetEdit, SetBankPreset, SetVoiceParam, SetGlobals, MuteOps,
    PitchBend, ModWheel, Breath, Foot, Aftertouch, Sustain, AllNotesOff, ArpEnable, SetArp,
    SeqEnable, SetPattern, SelectPattern, Play, Stop, ClockTick, ClockStart, ClockStop,
  } type;
  int a = 0, b = 0, c = 0;
  uint64_t stamp = 0;  // mach_absolute_time when queued
  union {
    Preset preset;
    GlobalSettings globals;
    ArpSettings arp;
    SeqPattern pattern;
  };
  Command() : preset() {}
  explicit Command(Type t, int a_ = 0, int b_ = 0) : type(t), a(a_), b(b_), preset() {}
};

// Notes the synth plays, for MIDI out and the key LEDs.
struct NoteEvent {
  uint8_t status, note, velocity;
};

class Synth {
 public:
  Synth();
  void prepare(double sampleRate);
  // Audio thread. Fills mono out.
  void render(float* out, int frames);

  // One queue per producer thread so each stays single producer.
  SpscQueue<Command, 512> uiQueue;
  SpscQueue<Command, 1024> midiQueue;
  SpscQueue<NoteEvent, 1024> noteOut;

  // Published for the UI thread.
  std::atomic<uint64_t> soundingLo{0}, soundingHi{0};
  std::atomic<int> seqStep{-1};
  std::atomic<int> seqPatternPlaying{-1};
  std::atomic<bool> playing{false};
  // Changes when the synth itself switches preset (sequencer voice binding).
  std::atomic<int> currentPreset{0};
  std::atomic<uint32_t> presetSwitches{0};
  std::atomic<int> activeVoices{0};
  std::atomic<float> cpuLoad{0};
  std::atomic<float> peak{0};
  // Time the last note-on waited in the queue before the audio thread took it.
  std::atomic<uint64_t> lastNoteQueueNs{0};
  static constexpr int kScopeSize = 4096;
  float scope[kScopeSize] = {};
  std::atomic<uint32_t> scopePos{0};

 private:
  struct VoiceSlot {
    Dx7Voice dx;
    int note = -1;
    uint32_t serial = 0;  // identifies one note-on, for timed note-offs
    bool keyDown = false;
    bool sustained = false;
    bool active = false;
    int envStage = 4;  // ENV overlay: 0 attack, 1 decay, 2 sustain, 3 release, 4 off
    float envLevel = 0;
  };

  void handle(const Command& c);
  // local is true for the on-board keys; MIDI input is not echoed to MIDI out.
  void noteOn(int note, int velocity, bool local);
  void noteOff(int note, bool local);
  // Returns the serial of the note it started.
  uint32_t playNote(int note, int velocity);
  void releaseSerial(uint32_t serial, int note);
  void releaseNote(int note);
  void keyupVoice(VoiceSlot& v);
  void allNotesOff();
  void loadPreset(int index, bool fromSeq);
  void applyEdit();
  void renderBlock();
  float envStep(int value, bool release) const;
  void advanceClock(int frames);
  void startClock();
  void doStep();
  void stepArp();
  void stepSeq();
  void scheduleNoteOff(int note, uint32_t serial, double samples);
  void updateSounding();
  int arpNextNote();
  void emit(uint8_t status, uint8_t note, uint8_t vel);
  double stepSamples(int rate) const;
  bool externalClock() const;
  int currentRate() const;

  double sr_ = 48000;
  Preset bank_[kNumPresets];
  int current_ = 0;
  Preset edit_;
  uint8_t voice_[kVoiceSize];  // edit voice with operator mutes applied
  FxChain fxChain_;
  GlobalSettings g_;
  Controllers ctrls_;
  Lfo lfo_;
  VoiceSlot voices_[kMaxPolyphony];
  int cursor_ = 0;
  bool sustain_ = false;
  int opMute_ = 0;
  int monoStack_[32];
  int monoCount_ = 0;
  int lastNote_ = -1;

  bool arpOn_ = false;
  ArpSettings arp_;
  int arpHeld_[32];
  int arpHeldVel_[32];
  int arpHeldCount_ = 0;
  bool arpKeyDown_[128] = {};
  int arpIndex_ = 0;
  uint32_t rng_ = 0x1234567;

  bool seqOn_ = false;
  SeqPattern patterns_[kSeqPatterns];
  int selectedPattern_ = 0;
  int playPattern_ = 0;
  int step_ = 0;
  int chainPos_ = 0;

  bool running_ = false;
  double phase_ = 0;
  int stepCount_ = 0;
  int clockTicks_ = 0;

  struct PendingOff {
    int note;
    uint32_t serial;
    double samples;
  };
  static constexpr int kMaxPendingOffs = 256;
  PendingOff pendingOffs_[kMaxPendingOffs];
  uint32_t nextSerial_ = 0;
  // Length in samples of the step being played, after swing.
  double curStepLen_ = 0;
  // How late, in samples, the current step fired after its due time.
  double stepLateness_ = 0;
  int pendingCount_ = 0;

  // Output stage
  float hpState_ = 0, hpPrev_ = 0, lpState_ = 0;
  float hpCoef_ = 1, lpCoef_ = 1;

  int32_t acc_[N];
  int32_t tmp_[N];
  float block_[N];
  int blockPos_ = N;
};

}  // namespace mf1
