#include "songs.h"

#include <cstring>

namespace mf1 {

namespace {

struct Op {
  uint8_t rates[4], levels[4];
  uint8_t outLevel;
  uint8_t coarse = 1, fine = 0, detune = 7;
  uint8_t breakPoint = 39, leftDepth = 0, rightDepth = 0, leftCurve = 0, rightCurve = 0;
  uint8_t rateScale = 0, ampModSens = 0, velSens = 0;
};

// Fills a VCED voice; ops are listed OP1 first.
void makeVoice(uint8_t v[kVoiceSize], const Op (&ops)[6], int algorithm, int feedback, const char* name) {
  memcpy(v, kInitVoice, kVoiceSize);
  for (int i = 0; i < 6; i++) {
    const Op& o = ops[i];
    uint8_t* p = v + (5 - i) * 21;  // VCED stores OP6 first
    for (int k = 0; k < 4; k++) {
      p[k] = o.rates[k];
      p[4 + k] = o.levels[k];
    }
    p[8] = o.breakPoint;
    p[9] = o.leftDepth;
    p[10] = o.rightDepth;
    p[11] = o.leftCurve;
    p[12] = o.rightCurve;
    p[13] = o.rateScale;
    p[14] = o.ampModSens;
    p[15] = o.velSens;
    p[16] = o.outLevel;
    p[17] = 0;
    p[18] = o.coarse;
    p[19] = o.fine;
    p[20] = o.detune;
  }
  v[134] = (uint8_t)(algorithm - 1);
  v[135] = (uint8_t)feedback;
  v[136] = 1;
  v[137] = 30;  // LFO speed, unused: no LFO depth
  v[139] = 0;
  v[140] = 0;
  size_t n = strlen(name);
  for (int i = 0; i < 10; i++) v[145 + i] = (uint8_t)(i < (int)n ? name[i] : ' ');
  sanitizeVoice(v);
}

// The slow-release synth of "Everything In Its Right Place": on the record
// most likely a Prophet-5 sawtooth with a short attack, about 450 ms decay to
// half level and a 1.5 s release. Its brightness is matched to the record
// from 150 Hz to 2.5 kHz (within about 4 dB). A slightly detuned pair gives
// width, a third pair an octave down adds weight, and the modulator envelopes
// fall the way a closing filter would.
Song rightPlace() {
  Song s;
  s.name = "Everything In Its Right Place";
  s.tempo = 124;
  s.width = 100;  // extended mode only; the FM-1 itself is mono
  const uint8_t car[4] = {75, 50, 30, 40}, carL[4] = {99, 97, 95, 0};
  const uint8_t mod[4] = {72, 50, 30, 40}, modL[4] = {77, 77, 77, 0};
  Op o1{{car[0], car[1], car[2], car[3]}, {carL[0], carL[1], carL[2], carL[3]}, 99};
  Op o2{{mod[0], mod[1], mod[2], mod[3]}, {modL[0], modL[1], modL[2], modL[3]}, 86};
  Op o3 = o1, o4 = o2, o5 = o1, o6 = o2;
  // Detune at its lowest puts the voice about 19 cents flat on FM-1 V15
  // (2.8 cents a step), the tuning of the record; each modulator follows its
  // carrier so the pairs stay harmonic. A clean DX7 goes only 7 cents flat.
  o1.detune = o2.detune = 0;
  o3.detune = o4.detune = 1;
  o3.outLevel = 88;
  o4.outLevel = 93;
  o4.coarse = 2;  // at twice the pitch, for the saw's upper harmonics
  // The third pair an octave down, as a sub oscillator for weight.
  o5.detune = o6.detune = 0;
  o5.coarse = 0;
  o5.outLevel = 85;
  o6.coarse = 0;
  o6.outLevel = 70;
  for (Op* m : {&o2, &o4, &o6}) {
    // Steady brightness, like the record's held buzz; a little darker up
    // the keyboard.
    m->velSens = 0;
    m->rightDepth = 22;
    m->rightCurve = 0;
    m->rateScale = 2;
  }
  for (Op* c : {&o1, &o3, &o5}) {
    c->velSens = 1;
    c->rateScale = 2;
  }
  const Op ops[6] = {o1, o2, o3, o4, o5, o6};
  makeVoice(s.voice, ops, 5, 7, "RIGHTPLACE");
  // A small room. The voice itself is voiced to the record's brightness.
  s.fx = {{0, 0}, {4, 1}, {5, 0}, {6, 38}, {7, 18}};

  // Chords from published analyses: triads with a high C held on top
  // over each root, in a 6 + 4 beat grouping. An arrangement of the chord
  // part, not a note for note transcription.
  const int C[] = {48, 64, 67, 72}, Db[] = {49, 65, 68, 72}, Eb[] = {51, 67, 70, 72}, F[] = {53, 60, 65, 69, 72};
  const int beat = kSongTicksPerBeat;
  uint32_t t = 0;
  auto chord = [&](const int* notes, int count, int beats) {
    for (int i = 0; i < count; i++) {
      uint8_t vel = i == 0 ? 82 : (notes[i] == 72 ? 96 : 88);
      // Released a sixteenth early so the next chord is struck afresh.
      s.events.push_back({t, (uint16_t)(beats * beat - beat / 4), (uint8_t)notes[i], vel});
    }
    t += beats * beat;
  };
  auto intro = [&] {
    chord(C, 4, 6);
    chord(Db, 4, 4);
    chord(C, 4, 6);
    chord(Eb, 4, 4);
  };
  auto verse = [&] {
    chord(C, 4, 6);
    chord(Db, 4, 4);
    chord(C, 4, 6);
    chord(Eb, 4, 2);
    chord(F, 5, 2);
  };
  auto bridge = [&] {
    chord(Db, 4, 5);
    chord(C, 4, 4);
    chord(Eb, 4, 6);
    chord(C, 4, 4);
  };
  for (int i = 0; i < 2; i++) intro();
  for (int i = 0; i < 4; i++) verse();
  for (int i = 0; i < 2; i++) bridge();
  for (int i = 0; i < 2; i++) verse();
  for (int i = 0; i < 2; i++) intro();
  chord(C, 4, 8);
  s.length = t + 2 * beat;
  return s;
}

}  // namespace

const std::vector<Song>& songs() {
  static const std::vector<Song> all = {rightPlace()};
  return all;
}

}  // namespace mf1
