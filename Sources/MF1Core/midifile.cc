#include "midifile.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace mf1 {

namespace {

struct Reader {
  const uint8_t* p;
  const uint8_t* end;
  bool ok = true;
  uint32_t byte() {
    if (p >= end) {
      ok = false;
      return 0;
    }
    return *p++;
  }
  uint32_t be(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 8) | byte();
    return v;
  }
  uint32_t vlq() {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
      uint32_t c = byte();
      v = (v << 7) | (c & 0x7f);
      if (!(c & 0x80)) break;
    }
    return v;
  }
};

struct RawNote {
  uint64_t start, end;  // file ticks
  uint8_t note, velocity;
};

}  // namespace

bool readMidiFile(const uint8_t* data, size_t len, MidiFileSong& out) {
  Reader r{data, data + len};
  if (len < 14 || r.be(4) != 0x4d546864) return false;  // "MThd"
  uint32_t headerLen = r.be(4);
  uint32_t format = r.be(2), tracks = r.be(2), division = r.be(2);
  if (!r.ok || format > 1 || (division & 0x8000) || division == 0) return false;
  r.p = data + 8 + headerLen;

  std::map<uint64_t, uint32_t> tempoMap;  // file tick -> microseconds per beat
  std::vector<RawNote> notes;
  for (uint32_t t = 0; t < tracks && r.ok && r.p + 8 <= r.end; t++) {
    uint32_t id = r.be(4), size = r.be(4);
    const uint8_t* trackEnd = std::min(r.end, r.p + size);
    if (id != 0x4d54726b) {  // not "MTrk": skip
      r.p = trackEnd;
      continue;
    }
    Reader tr{r.p, trackEnd};
    uint64_t tick = 0;
    uint32_t status = 0;
    std::map<int, std::vector<std::pair<uint64_t, uint8_t>>> open;  // channel*128+note -> starts
    while (tr.ok && tr.p < tr.end) {
      tick += tr.vlq();
      uint32_t b = tr.byte();
      if (b == 0xff) {
        uint32_t type = tr.byte(), n = tr.vlq();
        if (type == 0x51 && n == 3) {
          tempoMap[tick] = tr.be(3);
        } else {
          tr.p = std::min(tr.end, tr.p + n);
        }
        if (type == 0x2f) break;
        continue;
      }
      if (b == 0xf0 || b == 0xf7) {
        tr.p = std::min(tr.end, tr.p + tr.vlq());
        continue;
      }
      uint32_t d1;
      if (b & 0x80) {
        status = b;
        d1 = tr.byte();
      } else {
        d1 = b;  // running status
      }
      uint32_t type = status & 0xf0, ch = status & 0x0f;
      if (type == 0xc0 || type == 0xd0) continue;
      uint32_t d2 = tr.byte();
      if (type != 0x80 && type != 0x90) continue;
      if (ch == 9) continue;  // drums
      int key = (int)(ch * 128 + (d1 & 0x7f));
      if (type == 0x90 && d2 > 0) {
        open[key].push_back({tick, (uint8_t)d2});
      } else if (!open[key].empty()) {
        auto [start, vel] = open[key].front();
        open[key].erase(open[key].begin());
        notes.push_back({start, tick, (uint8_t)(d1 & 0x7f), vel});
      }
    }
    r.p = trackEnd;
  }
  if (notes.empty()) return false;

  // Retime through the tempo map into seconds, then into song ticks at the
  // file's first tempo, so the song plays at one steady rate.
  // Before the first tempo event the file runs at its first tempo, or the
  // MIDI default of 120 BPM when it has none.
  uint32_t firstTempo = tempoMap.empty() ? 500000 : tempoMap.begin()->second;
  tempoMap.emplace(0, firstTempo);
  out.tempo = 60e6 / tempoMap.begin()->second;
  auto seconds = [&](uint64_t tick) {
    double s = 0;
    auto it = tempoMap.begin();
    while (it != tempoMap.end()) {
      auto next = std::next(it);
      uint64_t segEnd = next == tempoMap.end() || next->first > tick ? tick : next->first;
      s += (double)(segEnd - it->first) * it->second / 1e6 / division;
      if (segEnd == tick) break;
      it = next;
    }
    return s;
  };
  double ticksPerSecond = out.tempo / 60.0 * kSongTicksPerBeat;
  std::sort(notes.begin(), notes.end(), [](const RawNote& a, const RawNote& b) { return a.start < b.start; });
  out.events.clear();
  for (const RawNote& n : notes) {
    if ((int)out.events.size() >= kMaxSongEvents) break;
    uint32_t start = (uint32_t)std::lround(seconds(n.start) * ticksPerSecond);
    uint32_t end = (uint32_t)std::lround(seconds(n.end) * ticksPerSecond);
    uint32_t length = std::max<uint32_t>(1, std::min<uint32_t>(65535, end - start));
    out.events.push_back({start, (uint16_t)length, n.note, n.velocity});
    out.length = std::max(out.length, start + length);
  }
  out.length += kSongTicksPerBeat;
  return true;
}

}  // namespace mf1
