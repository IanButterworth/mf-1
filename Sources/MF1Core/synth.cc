#include "synth.h"

#include <mach/mach_time.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mf1 {

const char* const kRateNames[kNumRates] = {"1/1", "1/2",   "1/4",  "1/4T",  "1/8",
                                           "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
const double kRateWholeNotes[kNumRates] = {1.0,          0.5,    0.25,         0.25 * 2 / 3, 0.125,
                                           0.125 * 2 / 3, 0.0625, 0.0625 * 2 / 3, 0.03125, 0.03125 * 2 / 3};
const char* const kArpModeNames[kArpModeCount] = {"Up",     "Down",  "Inclusive", "Exclusive",
                                                  "Random", "Order", "Repeat"};

SeqPattern::SeqPattern() {
  memset(steps, 0, sizeof(steps));
  for (auto& s : steps) {
    s.length = 1;
    s.rate = kDefaultRate;
  }
}

bool SeqPattern::isEmpty() const {
  for (const auto& s : steps)
    for (int n = 0; n < kStepNotes; n++)
      if (s.notes[n]) return false;
  return true;
}

Synth::Synth() {
  for (auto& p : bank_) memcpy(p.voice, kInitVoice, kVoiceSize);
  memcpy(edit_.voice, kInitVoice, kVoiceSize);
  memcpy(voice_, kInitVoice, kVoiceSize);
  memset(acc_, 0, sizeof(acc_));
  mach_timebase_info_data_t t;
  mach_timebase_info(&t);
  nsPerTick_ = (double)t.numer / t.denom;
}

void Synth::prepare(double sampleRate) {
  sr_ = sampleRate;
  Tables::init(sampleRate);
  fxChain_.prepare(sampleRate);
  fxChain_.setParams(edit_.fx);
  lfo_.reset(voice_ + 137);
  // The FM-1 line out: one pole 20 Hz high-pass and about 5.6 kHz low-pass.
  hpCoef_ = (float)std::exp(-2 * M_PI * 20.0 / sampleRate);
  lpCoef_ = (float)(1 - std::exp(-2 * M_PI * 5600.0 / sampleRate));
}

bool Synth::externalClock() const {
  if (seqOn_) return patterns_[playPattern_].sync;
  return arp_.sync;
}

int Synth::currentRate() const {
  if (seqOn_) {
    const SeqPattern& p = patterns_[playPattern_];
    return p.steps[std::min(step_, kSeqSteps - 1)].rate;
  }
  return arp_.rate;
}

double Synth::stepSamples(int rate) const {
  double beats = kRateWholeNotes[std::max(0, std::min(kNumRates - 1, rate))] * 4.0;
  return beats * 60.0 / std::max(30, std::min(300, g_.tempo)) * sr_;
}

// MIDI clock ticks, 24 to the quarter note, of the step with the given count.
int Synth::stepTicks(int rate, int count) const {
  int base = (int)std::lround(96.0 * kRateWholeNotes[std::max(0, std::min(kNumRates - 1, rate))]);
  int swing = seqOn_ ? patterns_[playPattern_].swing : arp_.swing;
  int on = (int)std::lround(2 * base * std::max(50, std::min(75, swing)) / 100.0);
  return (count & 1) ? 2 * base - on : on;
}

// Samples of the step with the given count, after swing.
double Synth::stepLength(int rate, int count) const {
  if (externalClock() && tickSamples_ > 0) return stepTicks(rate, count) * tickSamples_;
  double base = stepSamples(rate);
  int swing = seqOn_ ? patterns_[playPattern_].swing : arp_.swing;
  // Swing lengthens the on-beat step and shortens the off-beat one.
  double sw = std::max(50, std::min(75, swing)) / 100.0;
  return (count & 1) ? base * (2.0 - 2.0 * sw) : base * 2.0 * sw;
}

void Synth::emit(uint8_t status, uint8_t note, uint8_t vel) { noteOut.push(NoteEvent{status, note, vel}); }

// ------------------------------------------------------------------ presets

void Synth::applyEdit() {
  memcpy(voice_, edit_.voice, kVoiceSize);
  int mute = opMute_;
  // Muting OPn on V15 silences OP(7-n); the quirk keeps that mapping.
  if (g_.fm1Quirks) {
    int r = 0;
    for (int i = 0; i < 6; i++)
      if (mute & (1 << i)) r |= 1 << (5 - i);
    mute = r;
  }
  // Bit 5 of the enable byte is OP1, bit 0 is OP6; mute bit i is OP(i+1).
  int enable = edit_.voice[kOpEnable];
  for (int i = 0; i < 6; i++)
    if (mute & (1 << i)) enable &= ~(1 << (5 - i));
  voice_[kOpEnable] = (uint8_t)enable;
  fxChain_.setParams(edit_.fx);
  lfo_.reset(voice_ + 137);
}

void Synth::loadPreset(int index, bool fromSeq) {
  current_ = std::max(0, std::min(kNumPresets - 1, index));
  bool wasMono = edit_.mono;
  edit_ = bank_[current_];
  opMute_ = 0;
  if (wasMono != edit_.mono) allNotesOff();
  applyEdit();
  currentPreset.store(current_, std::memory_order_relaxed);
  if (fromSeq) presetSwitches.fetch_add(1, std::memory_order_release);
  // Sounding notes keep their old voice data, as on the hardware.
}

// ------------------------------------------------------------------ voices

void Synth::keyupVoice(VoiceSlot& v) {
  v.dx.keyup();
  if (v.envStage < 3) v.envStage = 3;
}

uint32_t Synth::playNote(int note, int velocity) {
  note = std::max(0, std::min(127, note));
  lfo_.keydown();
  double glide = g_.glideTime / 100.0;
  if (edit_.mono) {
    for (int i = 0; i < monoCount_; i++)
      if (monoStack_[i].note == note) {
        memmove(&monoStack_[i], &monoStack_[i + 1], sizeof(MonoKey) * (monoCount_ - i - 1));
        monoCount_--;
        break;
      }
    // The sounding note must stay on the stack, so a full stack forgets its oldest key.
    if (monoCount_ == 32) memmove(&monoStack_[0], &monoStack_[1], sizeof(MonoKey) * --monoCount_);
    VoiceSlot& v = voices_[0];
    int from = lastNote_;
    if (v.active && v.keyDown) {
      // Legato: new pitch, envelopes keep running.
      v.dx.transfer(note);
      v.dx.update(voice_);
      if (glide > 0 && from >= 0) v.dx.setGlideFrom(from, glide);
    } else {
      v.dx.init(voice_, note, velocity, v.active, ctrls_);
      v.active = true;
      v.envStage = edit_.envOn ? 0 : 4;
      v.envLevel = 0;
      if (glide > 0 && g_.glideMode == kGlideFullTime && from >= 0) v.dx.setGlideFrom(from, glide);
    }
    v.note = note;
    v.keyDown = true;
    v.sustained = false;
    v.serial = ++nextSerial_;
    monoStack_[monoCount_++] = MonoKey{note, v.serial};
    lastNote_ = note;
    return v.serial;
  }
  // Round robin from a cursor: the first voice whose key is up, else the
  // voice at the cursor is stolen (msfa allocation, as in the firmware).
  int chosen = cursor_;
  for (int i = 0; i < kMaxPolyphony; i++) {
    int ix = (cursor_ + i) % kMaxPolyphony;
    if (!voices_[ix].keyDown) {
      chosen = ix;
      break;
    }
  }
  cursor_ = (chosen + 1) % kMaxPolyphony;
  VoiceSlot& v = voices_[chosen];
  v.dx.init(voice_, note, velocity, v.active, ctrls_);
  v.note = note;
  v.keyDown = true;
  v.sustained = false;
  v.active = true;
  v.envStage = edit_.envOn ? 0 : 4;
  v.envLevel = 0;
  v.serial = ++nextSerial_;
  lastNote_ = note;
  return v.serial;
}

// Removes one entry from the mono key stack. If it was the sounding note, the
// voice goes back to the latest key still held, or is released.
void Synth::monoDrop(int index) {
  MonoKey gone = monoStack_[index];
  memmove(&monoStack_[index], &monoStack_[index + 1], sizeof(MonoKey) * (monoCount_ - index - 1));
  monoCount_--;
  VoiceSlot& v = voices_[0];
  if (!v.active || !v.keyDown || v.serial != gone.serial) return;
  if (monoCount_ > 0) {
    // Legato, without retriggering.
    const MonoKey& back = monoStack_[monoCount_ - 1];
    v.dx.transfer(back.note);
    v.dx.update(voice_);
    if (g_.glideTime > 0) v.dx.setGlideFrom(gone.note, g_.glideTime / 100.0);
    v.note = back.note;
    v.serial = back.serial;
    lastNote_ = back.note;
    return;
  }
  v.keyDown = false;
  if (sustain_)
    v.sustained = true;
  else
    keyupVoice(v);
}

void Synth::releaseNote(int note) {
  if (edit_.mono) {
    for (int i = 0; i < monoCount_; i++)
      if (monoStack_[i].note == note) {
        monoDrop(i);
        break;
      }
    return;
  }
  for (auto& v : voices_) {
    if (v.active && v.keyDown && v.note == note) {
      v.keyDown = false;
      if (sustain_)
        v.sustained = true;
      else
        keyupVoice(v);
    }
  }
}

// A timed note-off releases only the note-on it belongs to, so it cannot cut
// a later note on the same pitch short.
void Synth::releaseSerial(uint32_t serial, int note) {
  if (edit_.mono) {
    for (int i = 0; i < monoCount_; i++)
      if (monoStack_[i].serial == serial) {
        monoDrop(i);
        break;
      }
    return;
  }
  for (auto& v : voices_) {
    if (v.active && v.keyDown && v.serial == serial) {
      v.keyDown = false;
      if (sustain_)
        v.sustained = true;
      else
        keyupVoice(v);
    }
  }
}

void Synth::allNotesOff() {
  for (auto& v : voices_) {
    if (v.active && (v.keyDown || v.sustained)) keyupVoice(v);
    v.keyDown = false;
    v.sustained = false;
  }
  for (int i = 0; i < pendingCount_; i++) emit(0x80, (uint8_t)pendingOffs_[i].note, 0);
  pendingCount_ = 0;
  monoCount_ = 0;
  arpHeldCount_ = 0;
  memset(arpKeyDown_, 0, sizeof(arpKeyDown_));
}

void Synth::noteOn(int note, int velocity, bool local) {
  if (arpOn_) {
    bool anyDown = false;
    for (bool d : arpKeyDown_) anyDown |= d;
    // With latch on, a fresh chord replaces the latched one.
    if (arp_.latch && !anyDown) arpHeldCount_ = 0;
    arpKeyDown_[note & 127] = true;
    for (int i = 0; i < arpHeldCount_; i++)
      if (arpHeld_[i] == note) return;
    if (arpHeldCount_ < 32) {
      arpHeld_[arpHeldCount_] = note;
      arpHeldVel_[arpHeldCount_] = velocity;
      arpHeldCount_++;
    }
    if (!running_) {
      arpIndex_ = 0;
      startClock();
    }
    return;
  }
  playNote(note, velocity);
  if (local) emit(0x90, (uint8_t)note, (uint8_t)velocity);
}

void Synth::noteOff(int note, bool local) {
  // A key held from before ARP was switched on still releases normally.
  if (arpOn_ && arpKeyDown_[note & 127]) {
    arpKeyDown_[note & 127] = false;
    if (!arp_.latch) {
      for (int i = 0; i < arpHeldCount_; i++)
        if (arpHeld_[i] == note) {
          memmove(&arpHeld_[i], &arpHeld_[i + 1], sizeof(int) * (arpHeldCount_ - i - 1));
          memmove(&arpHeldVel_[i], &arpHeldVel_[i + 1], sizeof(int) * (arpHeldCount_ - i - 1));
          arpHeldCount_--;
          break;
        }
      if (arpHeldCount_ == 0) running_ = false;
    }
    return;
  }
  releaseNote(note);
  if (local) emit(0x80, (uint8_t)note, 0);
}

void Synth::scheduleNoteOff(int note, uint32_t serial, double samples) {
  if (pendingCount_ == kMaxPendingOffs) {
    // Full: release the entry due soonest now rather than leave a note stuck.
    int soonest = 0;
    for (int i = 1; i < pendingCount_; i++)
      if (pendingOffs_[i].samples < pendingOffs_[soonest].samples) soonest = i;
    releaseSerial(pendingOffs_[soonest].serial, pendingOffs_[soonest].note);
    emit(0x80, (uint8_t)pendingOffs_[soonest].note, 0);
    pendingOffs_[soonest] = pendingOffs_[--pendingCount_];
  }
  // Steps fire on block boundaries; count the length from the due time.
  pendingOffs_[pendingCount_++] = PendingOff{note, serial, samples - stepLateness_};
}

// ------------------------------------------------------------------ arp

int Synth::arpNextNote(int& velocity) {
  int n = arpHeldCount_;
  if (n == 0) return -1;
  // Sorts indexes rather than notes, so each note keeps its own velocity.
  int sorted[32];
  for (int i = 0; i < n; i++) sorted[i] = i;
  if (arp_.mode != kArpOrder)
    std::sort(sorted, sorted + n, [this](int a, int b) { return arpHeld_[a] < arpHeld_[b]; });
  int total = n * std::max(1, std::min(4, arp_.octaves));
  int idx = 0;
  switch (arp_.mode) {
    case kArpUp:
    case kArpOrder:
      idx = arpIndex_ % total;
      arpIndex_ = (arpIndex_ + 1) % total;
      break;
    case kArpDown:
      idx = total - 1 - (arpIndex_ % total);
      arpIndex_ = (arpIndex_ + 1) % total;
      break;
    case kArpInclusive: {
      // Up then down, playing the end notes twice.
      int cycle = total * 2;
      int i = arpIndex_ % cycle;
      idx = i < total ? i : cycle - 1 - i;
      arpIndex_ = (arpIndex_ + 1) % cycle;
      break;
    }
    case kArpExclusive: {
      // Up then down without repeating the end notes.
      int cycle = std::max(1, total * 2 - 2);
      int i = arpIndex_ % cycle;
      idx = total == 1 ? 0 : (i < total ? i : cycle - i);
      arpIndex_ = (arpIndex_ + 1) % cycle;
      break;
    }
    case kArpRandom:
      rng_ = rng_ * 1664525u + 1013904223u;
      idx = (int)((rng_ >> 16) % (uint32_t)total);
      break;
    case kArpRepeat:
      idx = (arpIndex_ / 2) % total;
      arpIndex_ = (arpIndex_ + 1) % (total * 2);
      break;
  }
  velocity = arpHeldVel_[sorted[idx % n]];
  return arpHeld_[sorted[idx % n]] + 12 * (idx / n);
}

void Synth::stepArp() {
  int vel = 0;
  int note = arpNextNote(vel);
  if (note < 0 || note > 127) return;
  double len = curStepLen_ * std::max(20, std::min(100, arp_.gate)) / 100.0;
  uint32_t serial = playNote(note, vel);
  emit(0x90, (uint8_t)note, (uint8_t)vel);
  // A full gate still releases a block before the next step is due, so the
  // release cannot land after the block that step fires in.
  scheduleNoteOff(note, serial, std::max(64.0, len - N));
}

// ------------------------------------------------------------------ seq

void Synth::switchChainPattern() {
  if (!seqOn_ || !chainDue_) return;
  chainDue_ = false;
  // Chain plays this many patterns in turn from the selected one.
  int chain = std::max(1, std::min(kSeqPatterns, patterns_[selectedPattern_].chain));
  chainPos_ = (chainPos_ + 1) % chain;
  int next = (selectedPattern_ + chainPos_) % kSeqPatterns;
  if (next != playPattern_) {
    playPattern_ = next;
    if (patterns_[next].voice != current_) loadPreset(patterns_[next].voice, true);
  }
  seqPatternPlaying.store(playPattern_, std::memory_order_relaxed);
}

void Synth::stepSeq() {
  SeqPattern& pat = patterns_[playPattern_];
  int length = std::max(1, std::min(kSeqSteps, pat.length));
  if (step_ >= length) step_ = 0;
  const SeqStep& s = pat.steps[step_];
  double stepLen = curStepLen_ > 0 ? curStepLen_ : stepSamples(s.rate);
  double gate = std::max(20, std::min(100, pat.gate)) / 100.0;
  // A long note ties over the next steps, each at its own rate; the gate cuts the last one.
  double len = stepLen, last = stepLen;
  for (int k = 1; k < std::max(1, (int)s.length); k++) {
    last = stepLength(pat.steps[(step_ + k) % length].rate, stepCount_ + k);
    len += last;
  }
  len -= last * (1 - gate);
  for (int i = 0; i < kStepNotes; i++) {
    if (!s.notes[i]) continue;
    int note = s.notes[i] + pat.transpose;
    if (note < 0 || note > 127) continue;
    int vel = s.velocity[i] ? s.velocity[i] : g_.keyVelocity;
    uint32_t serial = playNote(note, vel);
    emit(0x90, (uint8_t)note, (uint8_t)vel);
    scheduleNoteOff(note, serial, std::max(64.0, len - N));
  }
  seqStep.store(step_, std::memory_order_relaxed);
  step_++;
  if (step_ >= length) {
    step_ = 0;
    // The next pattern and its voice wait until its first step is due.
    chainDue_ = true;
  }
}

// ------------------------------------------------------------------ clock

void Synth::startClock() {
  stepCount_ = 0;
  ticksLeft_ = 0;
  phase_ = 0;
  chainDue_ = false;
  running_ = true;
}

void Synth::doStep() {
  if (seqOn_)
    stepSeq();
  else if (arpOn_)
    stepArp();
}

void Synth::advanceClock(int frames) {
  for (int i = 0; i < pendingCount_;) {
    pendingOffs_[i].samples -= frames;
    if (pendingOffs_[i].samples <= 0) {
      int note = pendingOffs_[i].note;
      releaseSerial(pendingOffs_[i].serial, note);
      emit(0x80, (uint8_t)note, 0);
      pendingOffs_[i] = pendingOffs_[--pendingCount_];
    } else {
      i++;
    }
  }
  if (!running_ || externalClock()) return;
  // A step due inside this block fires at its start, early by its offset.
  while (phase_ < frames && running_) {
    switchChainPattern();
    double len = stepLength(currentRate(), stepCount_);
    curStepLen_ = len;
    stepLateness_ = -phase_;
    doStep();
    stepLateness_ = 0;
    stepCount_++;
    phase_ += len;
  }
  phase_ -= frames;
}

// ------------------------------------------------------------------ commands

void Synth::handle(const Command& c) {
  switch (c.type) {
    case Command::NoteOn:
      if (c.stamp)
        lastNoteQueueNs.store((uint64_t)((mach_absolute_time() - c.stamp) * nsPerTick_), std::memory_order_relaxed);
      if (c.b == 0)
        noteOff(c.a, c.c != 0);
      else
        noteOn(c.a, c.b, c.c != 0);
      break;
    case Command::NoteOff:
      noteOff(c.a, c.c != 0);
      break;
    case Command::LoadPreset:
      loadPreset(c.a, false);
      break;
    case Command::SetEdit: {
      bool wasMono = edit_.mono;
      edit_ = c.preset;
      if (wasMono != edit_.mono) allNotesOff();
      applyEdit();
      if (!g_.fm1Quirks)
        for (auto& v : voices_)
          if (v.active) v.dx.update(voice_);
      break;
    }
    case Command::SetBankPreset:
      if (c.a >= 0 && c.a < kNumPresets) bank_[c.a] = c.preset;
      break;
    case Command::SetVoiceParam:
      if (c.a >= 0 && c.a < kVoiceSize) {
        edit_.voice[c.a] = (uint8_t)std::min(c.b, vcedMax(c.a));
        applyEdit();
        // On the FM-1 an edit only reaches the next note.
        if (!g_.fm1Quirks)
          for (auto& v : voices_)
            if (v.active) v.dx.update(voice_);
      }
      break;
    case Command::MuteOps:
      opMute_ = c.a & 0x3f;
      applyEdit();
      break;
    case Command::SetGlobals:
      g_ = c.globals;
      ctrls_.pitchBendUp = g_.bendUp;
      ctrls_.pitchBendDown = g_.bendDown;
      ctrls_.fm1Quirks = g_.fm1Quirks;
      // Stock V15 turns channel aftertouch into strong vibrato.
      ctrls_.aftertouchPitch = g_.fm1Quirks;
      ctrls_.aftertouchRange = g_.fm1Quirks ? 99 : 0;
      break;
    case Command::PitchBend:
      ctrls_.pitchBend = c.a;
      break;
    case Command::ModWheel:
      ctrls_.modWheel = c.a;
      break;
    case Command::Breath:
    case Command::Foot:
      break;
    case Command::Aftertouch:
      ctrls_.aftertouch = c.a;
      break;
    case Command::Sustain: {
      bool on = c.a >= 64;
      if (sustain_ && !on) {
        for (auto& v : voices_)
          if (v.sustained) {
            v.sustained = false;
            keyupVoice(v);
          }
      }
      sustain_ = on;
      break;
    }
    case Command::AllNotesOff:
      allNotesOff();
      break;
    case Command::ArpEnable:
      arpOn_ = c.a != 0;
      arpHeldCount_ = 0;
      memset(arpKeyDown_, 0, sizeof(arpKeyDown_));
      if (!seqOn_) running_ = false;
      break;
    case Command::SetArp:
      arp_ = c.arp;
      if (!arp_.latch) {
        // Unlatching drops the notes no longer held.
        int w = 0;
        for (int i = 0; i < arpHeldCount_; i++)
          if (arpKeyDown_[arpHeld_[i] & 127]) {
            arpHeld_[w] = arpHeld_[i];
            arpHeldVel_[w++] = arpHeldVel_[i];
          }
        arpHeldCount_ = w;
        if (arpOn_ && w == 0) running_ = false;
      }
      break;
    case Command::SeqEnable:
      seqOn_ = c.a != 0;
      running_ = false;
      playing.store(false);
      seqStep.store(-1);
      if (seqOn_) {
        playPattern_ = selectedPattern_;
        if (patterns_[playPattern_].voice != current_) loadPreset(patterns_[playPattern_].voice, true);
      }
      break;
    case Command::SetPattern:
      if (c.a >= 0 && c.a < kSeqPatterns) patterns_[c.a] = c.pattern;
      break;
    case Command::SelectPattern:
      selectedPattern_ = std::max(0, std::min(kSeqPatterns - 1, c.a));
      if (!running_) {
        playPattern_ = selectedPattern_;
        chainPos_ = 0;
        if (seqOn_ && patterns_[playPattern_].voice != current_)
          loadPreset(patterns_[playPattern_].voice, true);
      }
      break;
    case Command::Play:
      if (seqOn_) {
        playPattern_ = selectedPattern_;
        chainPos_ = 0;
        step_ = 0;
        if (patterns_[playPattern_].voice != current_) loadPreset(patterns_[playPattern_].voice, true);
        startClock();
        playing.store(true);
        seqPatternPlaying.store(playPattern_);
      }
      break;
    case Command::Stop:
      running_ = false;
      playing.store(false);
      seqStep.store(-1);
      for (int i = 0; i < pendingCount_; i++) {
        releaseSerial(pendingOffs_[i].serial, pendingOffs_[i].note);
        emit(0x80, (uint8_t)pendingOffs_[i].note, 0);
      }
      pendingCount_ = 0;
      break;
    case Command::ClockStart:
      // Continue restarts from step 0, as on the hardware.
      if (seqOn_ && patterns_[selectedPattern_].sync) {
        playPattern_ = selectedPattern_;
        chainPos_ = 0;
        step_ = 0;
        if (patterns_[playPattern_].voice != current_) loadPreset(patterns_[playPattern_].voice, true);
        startClock();
        playing.store(true);
        seqPatternPlaying.store(playPattern_);
      }
      if (arpOn_) {
        ticksLeft_ = 0;
        stepCount_ = 0;
      }
      break;
    case Command::ClockStop:
      if (seqOn_ && patterns_[playPattern_].sync) {
        running_ = false;
        playing.store(false);
        seqStep.store(-1);
      }
      break;
    case Command::ClockTick: {
      // Gates follow the clock's own tempo, a running average of the tick gaps.
      // A long gap means the clock paused, so it is left out.
      double gap = sampleTime_ - lastTickAt_;
      if (lastTickAt_ >= 0 && gap > 0 && gap < sr_ / 4 && (tickSamples_ == 0 || gap < 4 * tickSamples_))
        tickSamples_ = tickSamples_ > 0 ? tickSamples_ + (gap - tickSamples_) * 0.1 : gap;
      lastTickAt_ = sampleTime_;
      if (running_ && externalClock()) {
        if (ticksLeft_ <= 0) {
          switchChainPattern();
          int rate = currentRate();
          ticksLeft_ = stepTicks(rate, stepCount_);
          curStepLen_ = stepLength(rate, stepCount_);
          // Offs scheduled here also count down in this block's clock advance.
          stepLateness_ = -N;
          doStep();
          stepLateness_ = 0;
          stepCount_++;
        }
        ticksLeft_--;
      }
      break;
    }
  }
}

// ------------------------------------------------------------------ render

float Synth::envStep(int value, bool release) const {
  // 0..100 maps to 1 ms .. 5 s (10 s for release), exponentially.
  double seconds = 0.001 * std::pow(release ? 10000.0 : 5000.0, value / 100.0);
  return (float)(N / (seconds * sr_));
}

void Synth::renderBlock() {
  Command c;
  while (uiQueue.pop(c)) handle(c);
  while (midiQueue.pop(c)) handle(c);

  advanceClock(N);
  sampleTime_ += N;

  int32_t lfoVal = lfo_.getsample();
  int32_t lfoDelay = lfo_.getdelay();
  memset(acc_, 0, sizeof(acc_));
  int count = 0;
  float attack = envStep(edit_.env[0], false);
  float decay = envStep(edit_.env[1], false);
  float sustainLevel = edit_.env[2] / 100.f;
  float release = envStep(edit_.env[3], true);
  for (auto& v : voices_) {
    if (!v.active) continue;
    count++;
    memset(tmp_, 0, sizeof(tmp_));
    v.dx.compute(tmp_, lfoVal, lfoDelay, ctrls_);
    float start = v.envLevel, end = 1;
    if (v.envStage < 4) {
      // ENV: an ADSR on the whole voice, multiplied with the operator EGs.
      float& l = v.envLevel;
      switch (v.envStage) {
        case 0:
          l += attack;
          if (l >= 1) {
            l = 1;
            v.envStage = 1;
          }
          break;
        case 1:
          l -= decay;
          if (l <= sustainLevel) {
            l = sustainLevel;
            v.envStage = 2;
          }
          break;
        case 2:
          l = sustainLevel;
          break;
        case 3:
          l -= release;
          if (l <= 0) {
            l = 0;
            v.active = false;
          }
          break;
      }
      end = l;
    } else {
      start = 1;
    }
    // msfa output stage: each voice >> 4 and saturated, so 2^24 is full scale.
    for (int i = 0; i < N; i++) {
      int32_t s = tmp_[i] >> 4;
      if (start != 1 || end != 1) s = (int32_t)(s * (start + (end - start) * (i + 1) * (1.f / N)));
      acc_[i] += std::max(-(1 << 24), std::min((1 << 24) - 1, s));
    }
    if (!v.keyDown && !v.sustained && v.dx.isFinished()) v.active = false;
  }
  activeVoices.store(count, std::memory_order_relaxed);
}

void Synth::updateSounding() {
  uint64_t lo = 0, hi = 0;
  for (auto& v : voices_) {
    if (!v.active || v.note < 0 || !(v.keyDown || v.sustained)) continue;
    if (v.note < 64)
      lo |= 1ull << v.note;
    else
      hi |= 1ull << (v.note - 64);
  }
  soundingLo.store(lo, std::memory_order_relaxed);
  soundingHi.store(hi, std::memory_order_relaxed);
}

void Synth::render(float* out, int frames) {
  uint64_t t0 = mach_absolute_time();
  float vol = g_.masterVolume / 100.f;
  vol *= vol;
  float pk = 0;
  int done = 0;
  while (done < frames) {
    if (blockPos_ >= N) {
      renderBlock();
      for (int i = 0; i < N; i++) block_[i] = acc_[i] * (1.f / (float)(1 << 24));
      fxChain_.process(block_, N);
      bool q = g_.fm1Quirks;
      uint32_t sp = scopePos.load(std::memory_order_relaxed);
      for (int i = 0; i < N; i++) {
        float x = block_[i];
        if (q) {
          // The 16 bit DAC path, then the analog output filters.
          x = std::max(-1.f, std::min(32767.f / 32768.f, x));
          x = std::round(x * 32768.f) * (1.f / 32768.f);
          float hp = hpCoef_ * (hpState_ + x - hpPrev_);
          hpPrev_ = x;
          hpState_ = hp;
          lpState_ += lpCoef_ * (hp - lpState_);
          x = lpState_;
        }
        // The scope and peak are taken before MASTER, which on the
        // hardware is an analog pot after the digital signal path.
        scope[(sp + i) & (kScopeSize - 1)] = x;
        pk = std::max(pk, std::fabs(x));
        x *= vol;
        block_[i] = std::max(-1.f, std::min(1.f, x));
      }
      scopePos.store(sp + N, std::memory_order_release);
      blockPos_ = 0;
    }
    int n = std::min(frames - done, N - blockPos_);
    memcpy(out + done, block_ + blockPos_, sizeof(float) * n);
    blockPos_ += n;
    done += n;
  }
  updateSounding();
  peak.store(std::max(pk, peak.load(std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
  double ns = (double)(mach_absolute_time() - t0) * nsPerTick_;
  float load = (float)(ns / (frames / sr_ * 1e9));
  float prev = cpuLoad.load(std::memory_order_relaxed);
  cpuLoad.store(prev + (load - prev) * 0.05f, std::memory_order_relaxed);
}

}  // namespace mf1
