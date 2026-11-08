#pragma once
#include <cstdint>
#include <vector>

#include "WoofCore.h"

namespace woof {

// How a breed barks, in a few numbers the voice is built from. Pitch steps are the DX7 pitch envelope's, about
// 3/8 of a semitone each near the middle.
struct Breed {
  const char* voiceName;  // 10 characters at most
  const char* breed;
  const char* word;
  const char* blurb;
  uint32_t accent;
  int transpose;          // semitones from the key
  int formant;            // the mouth's fixed frequency, as the DX7 writes it: coarse * 100 + fine, 300 is 1 kHz
  int rasp;               // the breath's frequency, the same way
  int throat, mouth;      // modulator levels: how bright the throat is, how rich the mouth
  int mouthLevel;         // how loud the mouth is against the throat
  int breath;             // how loud the breath is at Breath 50
  int gruff;              // feedback on the breath at Gruff 50, 0..7
  int decay;              // how fast a bark's tail falls (a carrier's decay rate)
  int start, rise, fall;  // the pitch where a bark starts, peaks and ends, in pitch steps from the key
  bool mono = false;
  int arpMode = 0;        // how a chase runs on the arpeggiator: 0 up, 1 down, 2 inclusive, 4 random, 5 order
  int32_t day[WF_FIELDS] = {};
};

const std::vector<Breed>& breeds();
void packVoice(const uint8_t v[155], uint8_t out[128]);

}  // namespace woof
