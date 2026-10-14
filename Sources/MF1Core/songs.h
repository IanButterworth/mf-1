#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "synth.h"

namespace mf1 {

// A pre-programmed song: the voice and effects it plays with, and its notes.
struct Song {
  std::string name;
  double tempo;  // quarter notes per minute
  int width = -1;  // stereo width 0..100 in extended mode, -1 for the global setting
  uint8_t voice[kVoiceSize];
  std::vector<std::pair<int, int>> fx;  // FX CC number and value, panel resolution
  std::vector<SongEvent> events;
  uint32_t length;  // ticks
};

const std::vector<Song>& songs();

}  // namespace mf1
