#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "synth.h"

namespace mf1 {

// Notes read from a Standard MIDI File (format 0 or 1), retimed to song
// ticks at the given tempo so tempo changes in the file are kept.
struct MidiFileSong {
  std::vector<SongEvent> events;
  uint32_t length = 0;  // ticks
  double tempo = 120;
};

// Channel 10 (drums) is skipped. False if the file is not a MIDI file.
bool readMidiFile(const uint8_t* data, size_t len, MidiFileSong& out);

}  // namespace mf1
