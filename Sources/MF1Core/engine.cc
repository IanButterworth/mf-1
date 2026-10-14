// The control side: the saved "flash" state, the edit buffer, MIDI and
// SysEx handling, and the C interface. The audio thread only sees Commands.

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "MF1Core.h"
#include <mach/mach_time.h>
#include "platform_audio.h"
#include "platform_midi.h"
#include "synth.h"
#include "sysex.h"

using namespace mf1;

namespace {

constexpr uint32_t kStateMagic = 0x5331464d;  // "MF1S"
constexpr uint32_t kStateVersion = 1;

struct Engine {
  Synth synth;
  AudioOutput audio;
  MidiIO midi;
  std::string stateDir;
  std::string portName;
  bool started = false;
  bool offline = false;

  std::recursive_mutex lock;
  std::mutex sendLock;

  // "Flash": what SAVE and imports write to disk.
  Preset bank[kNumPresets];
  Preset imported[kNumPresets];
  bool haveFactory = false;
  SeqPattern patterns[kSeqPatterns];
  GlobalSettings globals;
  ArpSettings arp;

  // Working state.
  Preset edit;
  int current = 0;
  bool dirty = false;
  int opMutes = 0;
  int octave = 0, transpose = 0;
  bool arpOn = false, seqOn = false, recording = false;
  int selectedPattern = 0;
  int recStep = 0;
  int recHeld = 0;
  int recLongFrom = -1;
  uint32_t seenSwitches = 0;

  bool bankPending = false;
  uint8_t pendingVmem[32 * 128];

  std::atomic<uint32_t> generation{0};
  char message[64] = {};

  std::vector<std::vector<uint8_t>> offlineOut;
  std::string outputUID;

  bool startAudio(const std::string& uid) {
    return audio.start(
        [this](float* l, float* r, uint32_t n) {
          synth.render(l, (int)n);
          if (r) memcpy(r, l, sizeof(float) * n);
        },
        uid);
  }

  // Commands sent while no device rendered were dropped, so the synth is
  // brought back in line with the model after a restart.
  void restartAudio(const std::string& uid) {
    audio.stop();
    if (startAudio(uid)) resync();
  }

  void resync() {
    Preset keep = edit;
    bool wasDirty = dirty;
    int mutes = opMutes;
    pushAll();
    if (wasDirty) {
      edit = keep;
      dirty = true;
      pushEdit();
    }
    if (mutes) {
      opMutes = mutes;
      send(Command(Command::MuteOps, opMutes));
    }
  }
  std::thread noteThread;

  void midiSend(const uint8_t* d, size_t n) {
    if (offline)
      offlineOut.emplace_back(d, d + n);
    else
      midi.send(d, n);
  }
  std::atomic<bool> running{false};

  void send(Command c) {
    c.stamp = mach_absolute_time();
    std::lock_guard<std::mutex> g(sendLock);
    // Wait at most about 100 ms for the audio thread to make room, and drop
    // the command if the device has stopped rendering.
    for (int tries = 0; !synth.uiQueue.push(c); tries++) {
      if (!audio.running() || tries > 500) return;
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }

  void bump() { generation.fetch_add(1, std::memory_order_relaxed); }

  void setMessage(const char* m) {
    snprintf(message, sizeof(message), "%s", m);
    bump();
  }

  void pushEdit() {
    Command c(Command::SetEdit);
    c.preset = edit;
    send(c);
  }

  void pushBankPreset(int i) {
    Command c(Command::SetBankPreset, i);
    c.preset = bank[i];
    send(c);
  }

  void pushGlobals() {
    Command c(Command::SetGlobals);
    c.globals = globals;
    send(c);
  }

  void pushArp() {
    Command c(Command::SetArp);
    c.arp = arp;
    send(c);
  }

  void pushPattern(int i) {
    Command c(Command::SetPattern, i);
    c.pattern = patterns[i];
    send(c);
  }

  void loadPreset(int index) {
    current = std::max(0, std::min(kNumPresets - 1, index));
    edit = bank[current];
    dirty = false;
    opMutes = 0;
    send(Command(Command::LoadPreset, current));
  }

  // Follow preset switches the sequencer made on the audio thread.
  void syncFromSynth() {
    uint32_t s = synth.presetSwitches.load(std::memory_order_acquire);
    if (s != seenSwitches) {
      seenSwitches = s;
      current = synth.currentPreset.load(std::memory_order_relaxed);
      edit = bank[current];
      dirty = false;
      opMutes = 0;
      bump();
    }
  }

  // ---------------------------------------------------------------- disk

  template <typename T>
  static void put(std::vector<uint8_t>& out, const T& v) {
    auto* b = reinterpret_cast<const uint8_t*>(&v);
    out.insert(out.end(), b, b + sizeof(T));
  }

  struct Reader {
    const uint8_t* p;
    size_t left;
    bool take(void* dst, size_t n) {
      if (n > left) return false;
      memcpy(dst, p, n);
      p += n;
      left -= n;
      return true;
    }
  };

  static void writePreset(std::vector<uint8_t>& out, const Preset& p) {
    out.insert(out.end(), p.voice, p.voice + kVoiceSize);
    int32_t fx[24 + kFxCount];
    for (int i = 0; i < 24; i++) fx[i] = i == 3 ? p.fx.filterQ : p.fx.get(i);
    for (int i = 0; i < kFxCount; i++) fx[24 + i] = p.fx.order[i];
    put(out, fx);
    uint8_t extra[6] = {p.mono, p.envOn, p.env[0], p.env[1], p.env[2], p.env[3]};
    put(out, extra);
  }

  static bool readPreset(Reader& r, Preset& p) {
    int32_t fx[24 + kFxCount];
    uint8_t extra[6];
    if (!r.take(p.voice, kVoiceSize) || !r.take(fx, sizeof(fx)) || !r.take(extra, sizeof(extra))) return false;
    for (int i = 0; i < 24; i++) {
      if (i == 3)
        p.fx.filterQ = std::max(0, std::min(100, (int)fx[i]));
      else
        p.fx.set(i, fx[i]);
    }
    bool seen[kFxCount] = {};
    bool valid = true;
    for (int i = 0; i < kFxCount; i++) {
      int s = fx[24 + i];
      if (s < 0 || s >= kFxCount || seen[s]) valid = false;
      if (valid) seen[s] = true;
    }
    if (valid)
      for (int i = 0; i < kFxCount; i++) p.fx.order[i] = fx[24 + i];
    p.mono = extra[0];
    p.envOn = extra[1];
    for (int i = 0; i < 4; i++) p.env[i] = std::min<uint8_t>(100, extra[2 + i]);
    sanitizeVoice(p.voice);
    return true;
  }

  std::string statePath() const { return stateDir + "/state.bin"; }

  // Set when a state file could not be read and could not be moved aside
  // either; saving would then overwrite it.
  bool saveBlocked = false;
  // Pattern edits from recording are written by the note thread a little
  // later, so a burst of notes costs one write.
  std::atomic<bool> saveDue{false};
  uint64_t saveSeq = 0;
  std::mutex diskLock;
  uint64_t writtenSeq = 0;

  std::vector<uint8_t> serializeState() {
    std::vector<uint8_t> out;
    uint32_t head[3] = {kStateMagic, kStateVersion, (uint32_t)sizeof(SeqPattern)};
    put(out, head);
    for (auto& p : bank) writePreset(out, p);
    for (auto& p : imported) writePreset(out, p);
    int32_t g[16] = {globals.midiChannel, globals.fxChannel, globals.bendUp,     globals.bendDown,
                     globals.keyVelocity, globals.glideMode, globals.glideTime, globals.masterVolume,
                     globals.tempo,       globals.fm1Quirks, current,           transpose,
                     haveFactory,         0,                 0,                 0};
    put(out, g);
    put(out, patterns);
    return out;
  }

  // Writes a snapshot through a temporary file, so a failed write leaves
  // the previous state in place. Older snapshots than the one on disk are
  // skipped, as the note thread writes without the engine lock.
  bool writeState(const std::vector<uint8_t>& data, uint64_t seq) {
    std::lock_guard<std::mutex> g(diskLock);
    if (seq <= writtenSeq) return true;
    std::string tmp = statePath() + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp.c_str(), statePath().c_str()) != 0) {
      unlink(tmp.c_str());
      return false;
    }
    int dir = open(stateDir.c_str(), O_RDONLY);
    if (dir >= 0) {
      fsync(dir);
      close(dir);
    }
    writtenSeq = seq;
    return true;
  }

  bool saveState() {
    saveDue = false;
    if (stateDir.empty() || saveBlocked) return false;
    return writeState(serializeState(), ++saveSeq);
  }

  void saveSoon() {
    if (!stateDir.empty()) saveDue = true;
  }

  void flushSave() {
    if (saveDue) saveState();
  }

  static SeqPattern sanitizePattern(SeqPattern p) {
    auto c = [](int x, int lo, int hi) { return std::max(lo, std::min(hi, x)); };
    p.voice = c(p.voice, 0, kNumPresets - 1);
    p.rate = c(p.rate, 0, kNumRates - 1);
    p.gate = c(p.gate, 20, 100);
    p.swing = c(p.swing, 50, 75);
    p.sync = p.sync != 0;
    p.transpose = c(p.transpose, -24, 24);
    p.length = c(p.length, 1, kSeqSteps);
    p.chain = c(p.chain, 1, kSeqPatterns);
    for (auto& st : p.steps) {
      st.rate = (uint8_t)c(st.rate, 0, kNumRates - 1);
      st.length = (uint8_t)c(st.length, 1, kSeqSteps);
      for (auto& n : st.notes) n &= 0x7f;
      for (auto& v : st.velocity) v &= 0x7f;
    }
    return p;
  }

  enum class Load { Missing, Loaded, Unreadable };

  // Reads everything into temporaries and keeps it only if the whole file
  // made sense.
  Load loadState() {
    if (stateDir.empty()) return Load::Missing;
    FILE* f = fopen(statePath().c_str(), "rb");
    if (!f) return errno == ENOENT ? Load::Missing : Load::Unreadable;
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + got);
    bool ok = !ferror(f);
    fclose(f);
    Reader r{data.data(), data.size()};
    uint32_t head[3];
    ok = ok && r.take(head, sizeof(head)) && head[0] == kStateMagic && head[1] == kStateVersion;
    std::vector<Preset> b(bank, bank + kNumPresets), imp(imported, imported + kNumPresets);
    for (int i = 0; ok && i < kNumPresets; i++) ok = readPreset(r, b[i]);
    for (int i = 0; ok && i < kNumPresets; i++) ok = readPreset(r, imp[i]);
    int32_t g[16];
    ok = ok && r.take(g, sizeof(g));
    // Patterns are stored raw; skip them if the layout changed.
    std::vector<SeqPattern> pats;
    if (ok && head[2] == sizeof(SeqPattern)) {
      pats.resize(kSeqPatterns);
      ok = r.take(pats.data(), sizeof(SeqPattern) * kSeqPatterns);
    }
    if (!ok) return Load::Unreadable;
    auto clampi = [](int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); };
    globals.midiChannel = clampi(g[0], 0, 16);
    globals.fxChannel = clampi(g[1], 1, 16);
    globals.bendUp = clampi(g[2], 1, 24);
    globals.bendDown = clampi(g[3], 1, 24);
    globals.keyVelocity = clampi(g[4], 1, 127);
    globals.glideMode = clampi(g[5], 0, 1);
    globals.glideTime = clampi(g[6], 0, 100);
    globals.masterVolume = clampi(g[7], 0, 100);
    globals.tempo = clampi(g[8], 30, 300);
    globals.fm1Quirks = g[9] != 0;
    current = clampi(g[10], 0, kNumPresets - 1);
    transpose = clampi(g[11], -12, 12);
    haveFactory = g[12] != 0;
    std::copy(b.begin(), b.end(), bank);
    std::copy(imp.begin(), imp.end(), imported);
    for (size_t i = 0; i < pats.size(); i++) patterns[i] = sanitizePattern(pats[i]);
    return Load::Loaded;
  }

  // Moves an unreadable state file aside so nothing overwrites it. The
  // factory banks then load as on a first launch, since the copy is safe.
  void keepUnreadableState() {
    char stamp[32];
    time_t now = time(nullptr);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
    std::string aside = statePath() + ".unreadable-" + stamp;
    for (int i = 1; access(aside.c_str(), F_OK) == 0; i++)
      aside = statePath() + ".unreadable-" + stamp + "-" + std::to_string(i);
    saveBlocked = rename(statePath().c_str(), aside.c_str()) != 0;
    haveFactory = false;
    setMessage(saveBlocked ? "State unreadable: not saving" : "State unreadable: kept a copy");
  }

  bool openState() {
    switch (loadState()) {
      case Load::Loaded:
        return true;
      case Load::Unreadable:
        keepUnreadableState();
        return false;
      case Load::Missing:
        return false;
    }
    return false;
  }

  void pushAll() {
    for (int i = 0; i < kNumPresets; i++) pushBankPreset(i);
    for (int i = 0; i < kSeqPatterns; i++) pushPattern(i);
    pushGlobals();
    pushArp();
    loadPreset(current);
  }

  // ---------------------------------------------------------------- notes

  void keyDown(int note, int velocity, bool local) {
    if (note < 0 || note > 127) return;
    if (seqOn && recording) recordNote(note, velocity);
    Command c(Command::NoteOn, note, velocity);
    c.c = local;
    send(c);
  }

  void keyUp(int note, bool local) {
    if (note < 0 || note > 127) return;
    if (seqOn && recording) recordRelease();
    Command c(Command::NoteOff, note);
    c.c = local;
    send(c);
  }

  // Step recording: a chord pressed together fills one step, and the
  // record step moves on when all keys are up.
  void recordNote(int note, int velocity) {
    SeqPattern& p = patterns[selectedPattern];
    bool live = synth.playing.load();
    int step = live ? std::max(0, synth.seqStep.load()) : recStep;
    step = std::min(step, std::max(1, p.length) - 1);
    SeqStep& s = p.steps[step];
    if (recHeld == 0 || live) {
      if (!live || recHeld == 0) {
        memset(s.notes, 0, sizeof(s.notes));
        memset(s.velocity, 0, sizeof(s.velocity));
      }
      s.length = 1;
      s.rate = (uint8_t)p.rate;
      recLongFrom = step;
    }
    for (int i = 0; i < kStepNotes; i++) {
      if (s.notes[i] == note) break;
      if (!s.notes[i]) {
        s.notes[i] = (uint8_t)note;
        s.velocity[i] = (uint8_t)velocity;
        break;
      }
    }
    recHeld++;
    pushPattern(selectedPattern);
    saveSoon();
    bump();
  }

  void recordRelease() {
    if (recHeld <= 0) return;
    if (--recHeld == 0 && !synth.playing.load()) {
      SeqPattern& p = patterns[selectedPattern];
      int len = std::max(1, p.length);
      int from = recLongFrom >= 0 ? recLongFrom : recStep;
      recStep = (from + std::max(1, (int)p.steps[from].length)) % len;
      recLongFrom = -1;
      bump();
    }
  }

  // ---------------------------------------------------------------- MIDI in

  void handleMidi(const uint8_t* d, size_t len) {
    if (len == 0) return;
    uint8_t status = d[0];
    if (status == 0xf0) {
      handleSysex(d, len);
      return;
    }
    if (status == 0xf8) {
      send(Command(Command::ClockTick));
      return;
    }
    if (status == 0xfa || status == 0xfb) {
      send(Command(Command::ClockStart));
      return;
    }
    if (status == 0xfc) {
      send(Command(Command::ClockStop));
      return;
    }
    if (status >= 0xf0) return;
    int ch = (status & 0x0f) + 1;
    int type = status & 0xf0;
    int d1 = len > 1 ? d[1] : 0, d2 = len > 2 ? d[2] : 0;
    std::lock_guard<std::recursive_mutex> g(lock);
    // FX channel: CC 0..23 drive the effects; CC1 there is Filter Type.
    if (type == 0xb0 && ch == globals.fxChannel) {
      if (d1 <= 23) {
        // Values above a parameter's range clamp to its maximum.
        edit.fx.set(d1, d2);
        dirty = true;
        pushEdit();
        bump();
      }
      return;
    }
    if (globals.midiChannel != 0 && ch != globals.midiChannel) return;
    switch (type) {
      case 0x90:
        if (d2 == 0) {
          keyUp(d1, false);
        } else {
          // Stock V15 scales incoming velocity by 100/127.
          int v = globals.fm1Quirks ? std::max(1, d2 * 100 / 127) : d2;
          keyDown(d1, v, false);
        }
        break;
      case 0x80:
        keyUp(d1, false);
        break;
      case 0xc0:
        if (!seqOn) {
          loadPreset(d1);
          bump();
        }
        break;
      case 0xe0:
        send(Command(Command::PitchBend, d1 | (d2 << 7)));
        break;
      case 0xd0:
        send(Command(Command::Aftertouch, d1));
        break;
      case 0xb0:
        if (d1 == 1) send(Command(Command::ModWheel, d2));
        if (d1 == 2) send(Command(Command::Breath, d2));
        if (d1 == 4) send(Command(Command::Foot, d2));
        if (d1 == 64) send(Command(Command::Sustain, d2));
        break;
    }
  }

  // Returns 1 for a bank now waiting for A-D, 2 for a single voice loaded.
  int handleSysex(const uint8_t* d, size_t len) {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (len >= 7 && d[1] == 0x43) {
      // Parameter change: F0 43 10 gg pp vv F7, byte 2 exactly 0x10.
      if (len == 7 && d[2] == 0x10) {
        int addr = (d[3] << 7) + d[4];
        // The operator enable byte lies past the hardware's 155 byte buffer.
        if (addr < kOpEnable) {
          edit.voice[addr] = (uint8_t)std::min<int>(d[5], vcedMax(addr));
          dirty = true;
          send(Command(Command::SetVoiceParam, addr, edit.voice[addr]));
          bump();
        }
        return 0;
      }
      if ((d[2] & 0xf0) != 0) return 0;
      // 32 voice bulk: F0 43 0n 09 20 00 <4096> ck F7.
      if (len == 4104 && d[3] == 0x09 && d[4] == 0x20 && d[5] == 0x00) {
        if (dx7Checksum(d + 6, 4096) != d[4102]) {
          setMessage("Checksum Error");
          return 0;
        }
        memcpy(pendingVmem, d + 6, sizeof(pendingVmem));
        bankPending = true;
        bump();
        return 1;
      }
      // Single voice: F0 43 0n 00 01 1B <155> ck F7, into the edit buffer.
      if (len == 163 && d[3] == 0x00 && d[4] == 0x01 && d[5] == 0x1b) {
        if (dx7Checksum(d + 6, 155) != d[161]) {
          setMessage("Checksum Error");
          return 0;
        }
        memcpy(edit.voice, d + 6, 155);
        edit.voice[kOpEnable] = 0x3f;
        sanitizeVoice(edit.voice);
        dirty = true;
        pushEdit();
        bump();
        return 2;
      }
      return 0;
    }
    VendorPacket pkt;
    if (parseVendorMessage(d, len, pkt)) handleVendor(pkt);
    return 0;
  }

  // The M-VAVE private protocol, enough for the preset restore tool, voice
  // readers and librarians that identify the device first.
  void handleVendor(const VendorPacket& p) {
    auto reply = [&](uint8_t cmd, const std::vector<uint8_t>& payload) {
      auto msg = makeVendorMessage(cmd, payload);
      midiSend(msg.data(), msg.size());
    };
    auto u32 = [&](size_t at) {
      return (uint32_t)p.payload[at] | (uint32_t)p.payload[at + 1] << 8 | (uint32_t)p.payload[at + 2] << 16 |
             (uint32_t)p.payload[at + 3] << 24;
    };
    auto u24 = [&](size_t at) {
      return (uint32_t)p.payload[at] | (uint32_t)p.payload[at + 1] << 8 | (uint32_t)p.payload[at + 2] << 16;
    };
    switch (p.cmd) {
      case 0x11: {
        // Identity record. Tools check for this string, so it names the
        // firmware it is compatible with.
        std::vector<uint8_t> id(27, 0);
        const char* s = "FM-1_015";
        memcpy(id.data(), s, strlen(s));
        reply(0x11, id);
        break;
      }
      case 0x21:
        reply(0x00, {0x00});
        break;
      case 0x22: {
        // Write to object 5: a flat store of packed 128 byte voices.
        if (p.payload.size() < 8 || p.payload[0] != 0x05) {
          reply(0x00, {0x01});
          break;
        }
        uint32_t off = u32(1);
        uint32_t n = u24(5);
        if (p.payload.size() < 8 + n) {
          reply(0x00, {0x01});
          break;
        }
        for (uint32_t v = 0; v * 128 + 128 <= n; v++) {
          uint32_t slot = off / 128 + v;
          if (slot >= kNumPresets) break;
          uint8_t vced[kVoiceSize];
          unpackVmem(&p.payload[8 + v * 128], vced);
          memcpy(bank[slot].voice, vced, kVoiceSize);
          memcpy(imported[slot].voice, vced, kVoiceSize);
          pushBankPreset((int)slot);
          if ((int)slot == current && !dirty) {
            edit = bank[slot];
            pushEdit();
          }
        }
        saveState();
        bump();
        reply(0x00, {0x00});
        break;
      }
      case 0x23: {
        if (p.payload.size() < 8 || p.payload[0] != 0x05) break;
        uint32_t off = u32(1);
        uint32_t n = std::min<uint32_t>(u24(5), 16384);
        std::vector<uint8_t> out(p.payload.begin(), p.payload.begin() + 8);
        out[5] = n & 0xff;
        out[6] = (n >> 8) & 0xff;
        out[7] = (n >> 16) & 0xff;
        if (off == 0 && n == 155) {
          // Reading 155 bytes at 0 returns the edit buffer as VCED.
          out.insert(out.end(), edit.voice, edit.voice + 155);
        } else {
          for (uint32_t i = 0; i < n; i++) {
            uint32_t at = off + i;
            uint32_t slot = at / 128;
            uint8_t vm[128] = {};
            if (slot < kNumPresets) packVmem(bank[slot].voice, vm);
            out.push_back(slot < kNumPresets ? vm[at % 128] : 0);
          }
        }
        reply(0x23, out);
        break;
      }
      default:
        break;
    }
  }

  void drainNotes() {
    NoteEvent e;
    while (synth.noteOut.pop(e)) {
      // MIDI out carries notes only, on channel 1.
      uint8_t msg[3] = {e.status, e.note, e.velocity};
      midiSend(msg, 3);
    }
  }
};

Engine* E() {
  static Engine* e = new Engine;
  return e;
}

#define LOCK std::lock_guard<std::recursive_mutex> _g(E()->lock)

}  // namespace

// ------------------------------------------------------------------ lifecycle

bool mf1_start(const char* stateDir, const char* portName, const char* outputUID) {
  Engine* e = E();
  LOCK;
  if (e->started) return true;
  e->stateDir = stateDir ? stateDir : "";
  e->portName = portName ? portName : "MF-1";
  e->openState();
  e->audio.onPrepare([e](double sr) { e->synth.prepare(sr); });
  e->audio.onDeviceLost([e] {
    LOCK;
    e->restartAudio("");
  });
  e->started = true;
  e->pushAll();
  e->outputUID = outputUID ? outputUID : "";
  bool ok = e->startAudio(e->outputUID);
  e->midi.start(e->portName, [e](const uint8_t* d, size_t len) { e->handleMidi(d, len); });
  e->running = true;
  e->noteThread = std::thread([e] {
    auto lastSave = std::chrono::steady_clock::now();
    while (e->running.load()) {
      e->drainNotes();
      auto now = std::chrono::steady_clock::now();
      if (e->saveDue.load() && now - lastSave >= std::chrono::milliseconds(500)) {
        lastSave = now;
        std::vector<uint8_t> data;
        uint64_t seq = 0;
        {
          LOCK;
          if (e->saveDue.exchange(false) && !e->saveBlocked) {
            data = e->serializeState();
            seq = ++e->saveSeq;
          }
        }
        if (seq) e->writeState(data, seq);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  return ok;
}

void mf1_stop(void) {
  Engine* e = E();
  e->running = false;
  if (e->noteThread.joinable()) e->noteThread.join();
  {
    LOCK;
    e->flushSave();
  }
  e->audio.stop();
}

bool mf1_needs_factory(void) {
  LOCK;
  return !E()->haveFactory;
}

bool mf1_import_factory_bank(int bank, const uint8_t* data, size_t len) {
  Engine* e = E();
  LOCK;
  if (bank < 0 || bank > 3 || len != 4104 || data[0] != 0xf0 || data[1] != 0x43 || data[3] != 0x09) return false;
  for (int v = 0; v < 32; v++) {
    int slot = bank * 32 + v;
    unpackVmem(data + 6 + v * 128, e->bank[slot].voice);
    e->bank[slot].fx = FxParams();
    e->bank[slot].mono = false;
    e->bank[slot].envOn = false;
    e->imported[slot] = e->bank[slot];
    e->pushBankPreset(slot);
  }
  if (bank == 3) {
    e->haveFactory = true;
    e->loadPreset(e->current);
    e->saveState();
  }
  e->bump();
  return true;
}

// ------------------------------------------------------------------ keys

void mf1_key_down(int note) {
  LOCK;
  E()->keyDown(note, E()->globals.keyVelocity, true);
}

void mf1_key_up(int note) {
  LOCK;
  E()->keyUp(note, true);
}

void mf1_all_notes_off(void) { E()->send(Command(Command::AllNotesOff)); }
void mf1_pitch_bend(int v) { E()->send(Command(Command::PitchBend, v)); }
void mf1_mod_wheel(int v) { E()->send(Command(Command::ModWheel, v)); }

// ------------------------------------------------------------------ presets

int mf1_current_preset(void) {
  LOCK;
  E()->syncFromSynth();
  return E()->current;
}

void mf1_load_preset(int index) {
  LOCK;
  E()->loadPreset((index % kNumPresets + kNumPresets) % kNumPresets);
}

static void copyName(const uint8_t* v, char out[11]) {
  for (int i = 0; i < 10; i++) {
    uint8_t c = v[145 + i];
    out[i] = (c >= 32 && c < 127) ? (char)c : ' ';
  }
  out[10] = 0;
}

void mf1_preset_name(int index, char out[11]) {
  LOCK;
  copyName(E()->bank[std::max(0, std::min(kNumPresets - 1, index))].voice, out);
}

void mf1_edit_name(char out[11]) {
  LOCK;
  E()->syncFromSynth();
  copyName(E()->edit.voice, out);
}

int mf1_edit_get(int i) {
  LOCK;
  return i >= 0 && i < kVoiceSize ? E()->edit.voice[i] : 0;
}

void mf1_edit_set(int i, int value) {
  Engine* e = E();
  LOCK;
  if (i < 0 || i >= kVoiceSize) return;
  value = std::max(0, std::min(vcedMax(i), value));
  if (e->edit.voice[i] == value) return;
  e->edit.voice[i] = (uint8_t)value;
  e->dirty = true;
  e->send(Command(Command::SetVoiceParam, i, value));
}

bool mf1_edit_mono(void) {
  LOCK;
  return E()->edit.mono;
}

void mf1_edit_set_mono(bool mono) {
  Engine* e = E();
  LOCK;
  if (e->edit.mono == mono) return;
  e->edit.mono = mono;
  e->dirty = true;
  e->pushEdit();
}

int mf1_env_get(int index) {
  LOCK;
  if (index == 0) return E()->edit.envOn;
  return index >= 1 && index <= 4 ? E()->edit.env[index - 1] : 0;
}

void mf1_env_set(int index, int value) {
  Engine* e = E();
  LOCK;
  if (index == 0)
    e->edit.envOn = value != 0;
  else if (index >= 1 && index <= 4)
    e->edit.env[index - 1] = (uint8_t)std::max(0, std::min(100, value));
  e->dirty = true;
  e->pushEdit();
}

int mf1_fx_get(int cc) {
  LOCK;
  return cc == 3 ? E()->edit.fx.filterQ : E()->edit.fx.get(cc);
}

void mf1_fx_set(int cc, int value) {
  Engine* e = E();
  LOCK;
  if (cc == 3)
    e->edit.fx.filterQ = std::max(0, std::min(100, value));
  else
    e->edit.fx.set(cc, value);
  e->dirty = true;
  e->pushEdit();
}

int mf1_fx_order(int position) {
  LOCK;
  return E()->edit.fx.order[std::max(0, std::min(kFxCount - 1, position))];
}

void mf1_fx_swap(int a, int b) {
  Engine* e = E();
  LOCK;
  if (a < 0 || b < 0 || a >= kFxCount || b >= kFxCount) return;
  std::swap(e->edit.fx.order[a], e->edit.fx.order[b]);
  e->dirty = true;
  e->pushEdit();
}

bool mf1_dirty(void) {
  LOCK;
  return E()->dirty;
}

void mf1_save(void) {
  Engine* e = E();
  LOCK;
  e->syncFromSynth();
  e->bank[e->current] = e->edit;
  e->pushBankPreset(e->current);
  e->dirty = false;
  e->saveState();
  e->bump();
}

void mf1_init_voice(void) {
  Engine* e = E();
  LOCK;
  memcpy(e->edit.voice, kInitVoice, kVoiceSize);
  e->dirty = true;
  e->pushEdit();
}

void mf1_rename(const char* name) {
  Engine* e = E();
  LOCK;
  size_t n = strlen(name);
  for (int i = 0; i < 10; i++) {
    char c = i < (int)n ? name[i] : ' ';
    e->edit.voice[145 + i] = (uint8_t)((c >= 32 && c < 127) ? c : ' ');
  }
  e->dirty = true;
  e->pushEdit();
  e->bump();
}

void mf1_copy_voice(int from) {
  Engine* e = E();
  LOCK;
  from = std::max(0, std::min(kNumPresets - 1, from));
  e->edit = e->bank[from];
  e->dirty = true;
  e->pushEdit();
  e->bump();
}

void mf1_reset_bank(int bank) {
  Engine* e = E();
  LOCK;
  if (bank < 0 || bank > 3) return;
  for (int i = bank * 32; i < bank * 32 + 32; i++) {
    e->bank[i] = e->imported[i];
    e->bank[i].fx = FxParams();
    e->pushBankPreset(i);
  }
  if (e->current / 32 == bank) e->loadPreset(e->current);
  e->saveState();
  e->bump();
}

void mf1_set_op_mutes(int mask) {
  Engine* e = E();
  LOCK;
  e->opMutes = mask & 0x3f;
  e->send(Command(Command::MuteOps, e->opMutes));
}

int mf1_op_mutes(void) {
  LOCK;
  return E()->opMutes;
}

int mf1_carrier_mask(int algorithm) { return FmCore::carrierMask(algorithm); }

// ------------------------------------------------------------------ globals

int mf1_global_get(int id) {
  Engine* e = E();
  LOCK;
  const GlobalSettings& g = e->globals;
  switch (id) {
    case MF1_G_MIDI_CH: return g.midiChannel;
    case MF1_G_FX_CH: return g.fxChannel;
    case MF1_G_BEND_UP: return g.bendUp;
    case MF1_G_BEND_DOWN: return g.bendDown;
    case MF1_G_VELOCITY: return g.keyVelocity;
    case MF1_G_GLIDE_MODE: return g.glideMode;
    case MF1_G_GLIDE_TIME: return g.glideTime;
    case MF1_G_VOLUME: return g.masterVolume;
    case MF1_G_TEMPO: return g.tempo;
    case MF1_G_QUIRKS: return g.fm1Quirks;
    case MF1_G_OCTAVE: return e->octave;
    case MF1_G_TRANSPOSE: return e->transpose;
  }
  return 0;
}

void mf1_global_set(int id, int v) {
  Engine* e = E();
  LOCK;
  GlobalSettings& g = e->globals;
  auto c = [](int x, int lo, int hi) { return std::max(lo, std::min(hi, x)); };
  switch (id) {
    case MF1_G_MIDI_CH: g.midiChannel = c(v, 0, 16); break;
    case MF1_G_FX_CH: g.fxChannel = c(v, 1, 16); break;
    case MF1_G_BEND_UP: g.bendUp = c(v, 1, 24); break;
    case MF1_G_BEND_DOWN: g.bendDown = c(v, 1, 24); break;
    case MF1_G_VELOCITY: g.keyVelocity = c(v, 1, 127); break;
    case MF1_G_GLIDE_MODE: g.glideMode = c(v, 0, 1); break;
    case MF1_G_GLIDE_TIME: g.glideTime = c(v, 0, 100); break;
    case MF1_G_VOLUME: g.masterVolume = c(v, 0, 100); break;
    case MF1_G_TEMPO: g.tempo = c(v, 30, 300); break;
    case MF1_G_QUIRKS: g.fm1Quirks = v != 0; break;
    case MF1_G_OCTAVE: e->octave = c(v, -3, 3); return;
    case MF1_G_TRANSPOSE:
      e->transpose = c(v, -12, 12);
      e->saveState();
      return;
    default: return;
  }
  e->pushGlobals();
  // Device settings are written at once; only voice edits wait for SAVE,
  // so the quit prompt is about the current preset alone.
  e->saveState();
}

// ------------------------------------------------------------------ arp

bool mf1_arp_on(void) {
  LOCK;
  return E()->arpOn;
}

void mf1_arp_enable(bool on) {
  Engine* e = E();
  LOCK;
  e->arpOn = on;
  e->send(Command(Command::ArpEnable, on));
}

int mf1_arp_get(int id) {
  LOCK;
  const ArpSettings& a = E()->arp;
  switch (id) {
    case MF1_A_MODE: return a.mode;
    case MF1_A_OCTAVES: return a.octaves;
    case MF1_A_RATE: return a.rate;
    case MF1_A_GATE: return a.gate;
    case MF1_A_SWING: return a.swing;
    case MF1_A_LATCH: return a.latch;
    case MF1_A_SYNC: return a.sync;
  }
  return 0;
}

void mf1_arp_set(int id, int v) {
  Engine* e = E();
  LOCK;
  ArpSettings& a = e->arp;
  auto c = [](int x, int lo, int hi) { return std::max(lo, std::min(hi, x)); };
  switch (id) {
    case MF1_A_MODE: a.mode = c(v, 0, kArpModeCount - 1); break;
    case MF1_A_OCTAVES: a.octaves = c(v, 1, 4); break;
    case MF1_A_RATE: a.rate = c(v, 0, kNumRates - 1); break;
    case MF1_A_GATE: a.gate = c(v, 20, 100); break;
    case MF1_A_SWING: a.swing = c(v, 50, 75); break;
    case MF1_A_LATCH: a.latch = v != 0; break;
    case MF1_A_SYNC: a.sync = v != 0; break;
  }
  e->pushArp();
}

const char* mf1_arp_mode_name(int mode) { return kArpModeNames[std::max(0, std::min(kArpModeCount - 1, mode))]; }
const char* mf1_rate_name(int rate) { return kRateNames[std::max(0, std::min(kNumRates - 1, rate))]; }
int mf1_rate_count(void) { return kNumRates; }

// ------------------------------------------------------------------ seq

bool mf1_seq_on(void) {
  LOCK;
  return E()->seqOn;
}

void mf1_seq_enable(bool on) {
  Engine* e = E();
  LOCK;
  e->seqOn = on;
  e->recording = false;
  e->recHeld = 0;
  e->send(Command(Command::SeqEnable, on));
}

int mf1_seq_get(int id) {
  Engine* e = E();
  LOCK;
  const SeqPattern& p = e->patterns[e->selectedPattern];
  switch (id) {
    case MF1_S_PATTERN: return e->selectedPattern;
    case MF1_S_CHAIN: return p.chain;
    case MF1_S_STEPS: return p.length;
    case MF1_S_VOICE: return p.voice;
    case MF1_S_RATE: return p.rate;
    case MF1_S_GATE: return p.gate;
    case MF1_S_SWING: return p.swing;
    case MF1_S_SYNC: return p.sync;
    case MF1_S_TRANSPOSE: return p.transpose;
  }
  return 0;
}

void mf1_seq_set(int id, int v) {
  Engine* e = E();
  LOCK;
  auto c = [](int x, int lo, int hi) { return std::max(lo, std::min(hi, x)); };
  if (id == MF1_S_PATTERN) {
    e->selectedPattern = c(v, 0, kSeqPatterns - 1);
    e->recStep = 0;
    e->send(Command(Command::SelectPattern, e->selectedPattern));
    return;
  }
  SeqPattern& p = e->patterns[e->selectedPattern];
  switch (id) {
    case MF1_S_CHAIN: p.chain = c(v, 1, 16); break;
    case MF1_S_STEPS:
      p.length = c(v, 1, 16);
      e->recStep %= p.length;
      break;
    case MF1_S_VOICE:
      p.voice = c(v, 0, kNumPresets - 1);
      e->loadPreset(p.voice);
      break;
    case MF1_S_RATE:
      p.rate = c(v, 0, kNumRates - 1);
      // While recording the rate stamps the next steps; otherwise it
      // applies to the whole pattern.
      if (!e->recording)
        for (auto& s : p.steps) s.rate = (uint8_t)p.rate;
      break;
    case MF1_S_GATE: p.gate = c(v, 20, 100); break;
    case MF1_S_SWING: p.swing = c(v, 50, 75); break;
    case MF1_S_SYNC: p.sync = v != 0; break;
    case MF1_S_TRANSPOSE: p.transpose = c(v, -24, 24); break;
  }
  e->pushPattern(e->selectedPattern);
  e->saveState();
}

void mf1_seq_clear_pattern(void) {
  Engine* e = E();
  LOCK;
  SeqPattern& p = e->patterns[e->selectedPattern];
  SeqPattern blank;
  memcpy(p.steps, blank.steps, sizeof(p.steps));
  for (auto& s : p.steps) s.rate = (uint8_t)p.rate;
  e->recStep = 0;
  e->pushPattern(e->selectedPattern);
  e->saveState();
}

void mf1_seq_play(void) { E()->send(Command(Command::Play)); }

void mf1_seq_stop(void) {
  Engine* e = E();
  LOCK;
  e->recording = false;
  e->send(Command(Command::Stop));
  e->flushSave();
}

bool mf1_seq_playing(void) { return E()->synth.playing.load(); }

void mf1_seq_record(bool on) {
  Engine* e = E();
  LOCK;
  e->recording = on;
  e->recHeld = 0;
  e->recLongFrom = -1;
  if (!on) e->flushSave();
}

bool mf1_seq_recording(void) {
  LOCK;
  return E()->recording;
}

int mf1_seq_rec_step(void) {
  LOCK;
  return E()->recStep;
}

void mf1_seq_select(int delta) {
  Engine* e = E();
  LOCK;
  SeqPattern& p = e->patterns[e->selectedPattern];
  int len = std::max(1, p.length);
  if (e->recHeld > 0 && e->recLongFrom >= 0) {
    // Long note: stretch the held step towards the step SELECT points at.
    SeqStep& s = p.steps[e->recLongFrom];
    s.length = (uint8_t)std::max(1, std::min(len - e->recLongFrom, (int)s.length + delta));
    e->pushPattern(e->selectedPattern);
    e->saveState();
  } else {
    e->recStep = ((e->recStep + delta) % len + len) % len;
  }
  e->bump();
}

int mf1_seq_play_step(void) { return E()->synth.seqStep.load(); }

int mf1_seq_step_mask(void) {
  Engine* e = E();
  LOCK;
  const SeqPattern& p = e->patterns[e->selectedPattern];
  int mask = 0;
  for (int i = 0; i < kSeqSteps; i++) {
    for (int n = 0; n < kStepNotes; n++)
      if (p.steps[i].notes[n]) mask |= 1 << i;
    // Steps covered by a long note count as filled.
    if (p.steps[i].notes[0] && p.steps[i].length > 1)
      for (int k = 1; k < p.steps[i].length && i + k < kSeqSteps; k++) mask |= 1 << (i + k);
  }
  return mask;
}

// ------------------------------------------------------------------ SysEx files

int mf1_import_syx(const uint8_t* data, size_t len) {
  Engine* e = E();
  // A .syx file may hold several messages; take the first one we know.
  size_t i = 0;
  while (i < len) {
    if (data[i] != 0xf0) {
      i++;
      continue;
    }
    size_t j = i + 1;
    while (j < len && data[j] != 0xf7) j++;
    if (j >= len) break;
    size_t n = j - i + 1;
    if ((n == 4104 && data[i + 3] == 0x09) || (n == 163 && data[i + 3] == 0x00)) {
      int kind = e->handleSysex(data + i, n);
      if (kind) return kind;
    }
    i = j + 1;
  }
  // Raw 4096 byte VMEM files without a header are also common.
  if (len == 4096) {
    LOCK;
    memcpy(e->pendingVmem, data, 4096);
    e->bankPending = true;
    e->bump();
    return 1;
  }
  return 0;
}

bool mf1_bank_pending(void) {
  LOCK;
  return E()->bankPending;
}

void mf1_bank_pending_name(int voice, char out[11]) {
  Engine* e = E();
  LOCK;
  uint8_t vced[kVoiceSize];
  unpackVmem(e->pendingVmem + std::max(0, std::min(31, voice)) * 128, vced);
  copyName(vced, out);
}

void mf1_bank_commit(int bank) {
  Engine* e = E();
  LOCK;
  if (!e->bankPending || bank < 0 || bank > 3) return;
  for (int v = 0; v < 32; v++) {
    int slot = bank * 32 + v;
    unpackVmem(e->pendingVmem + v * 128, e->bank[slot].voice);
    memcpy(e->imported[slot].voice, e->bank[slot].voice, kVoiceSize);
    e->pushBankPreset(slot);
  }
  e->bankPending = false;
  if (e->current / 32 == bank) e->loadPreset(e->current);
  e->saveState();
  e->bump();
}

void mf1_bank_cancel(void) {
  LOCK;
  E()->bankPending = false;
}

void mf1_export_bank(int bank, uint8_t out[4104]) {
  Engine* e = E();
  LOCK;
  uint8_t vmem[32 * 128];
  bank = std::max(0, std::min(3, bank));
  for (int v = 0; v < 32; v++) packVmem(e->bank[bank * 32 + v].voice, vmem + v * 128);
  auto msg = makeBankDump(vmem);
  memcpy(out, msg.data(), 4104);
}

void mf1_export_voice(uint8_t out[163]) {
  LOCK;
  auto msg = makeVoiceDump(E()->edit.voice);
  memcpy(out, msg.data(), 163);
}

// ------------------------------------------------------------------ status

void mf1_sounding(uint64_t* lo, uint64_t* hi) {
  *lo = E()->synth.soundingLo.load(std::memory_order_relaxed);
  *hi = E()->synth.soundingHi.load(std::memory_order_relaxed);
}

void mf1_scope(float* out, int n) {
  Synth& s = E()->synth;
  n = std::min(n, Synth::kScopeSize);
  uint32_t end = s.scopePos.load(std::memory_order_acquire);
  for (int i = 0; i < n; i++) out[i] = s.scope[(end - n + i) & (Synth::kScopeSize - 1)];
}

float mf1_cpu_load(void) { return E()->synth.cpuLoad.load(); }
int mf1_active_voices(void) { return E()->synth.activeVoices.load(); }
float mf1_peak(void) { return E()->synth.peak.load(); }

uint32_t mf1_generation(void) {
  {
    LOCK;
    E()->syncFromSynth();
  }
  return E()->generation.load();
}

void mf1_take_message(char out[64]) {
  LOCK;
  memcpy(out, E()->message, 64);
  E()->message[0] = 0;
}

double mf1_sample_rate(void) { return E()->audio.sampleRate(); }
const char* mf1_midi_port_name(void) { return E()->portName.c_str(); }

// ------------------------------------------------------------------ offline

void mf1_offline_init(double sampleRate) {
  Engine* e = E();
  LOCK;
  e->offline = true;
  e->started = true;
  e->synth.prepare(sampleRate);
  e->pushAll();
}

void mf1_offline_render(float* out, int frames) {
  E()->synth.render(out, frames);
  E()->drainNotes();
}

void mf1_inject_midi(const uint8_t* data, size_t len) { E()->handleMidi(data, len); }

size_t mf1_take_midi_out(uint8_t* out, size_t max) {
  Engine* e = E();
  LOCK;
  if (e->offlineOut.empty()) return 0;
  auto msg = e->offlineOut.front();
  e->offlineOut.erase(e->offlineOut.begin());
  size_t n = std::min(max, msg.size());
  memcpy(out, msg.data(), n);
  return n;
}

bool mf1_offline_open_state(const char* stateDir) {
  Engine* e = E();
  LOCK;
  e->stateDir = stateDir ? stateDir : "";
  e->saveBlocked = false;
  bool ok = e->openState();
  if (ok) e->pushAll();
  return ok;
}

size_t mf1_midi_loopback(const uint8_t* data, size_t len, size_t listBytes, uint8_t* out, size_t max,
                         int* lists) {
  size_t n = 0;
  for (auto& m : midiLoopback(data, len, listBytes, lists)) {
    size_t k = std::min(max - n, m.size());
    memcpy(out + n, m.data(), k);
    n += k;
  }
  return n;
}

void mf1_latency(double out[6]) {
  Engine* e = E();
  auto l = e->audio.latency();
  double sr = e->audio.sampleRate();
  out[0] = l.bufferFrames;
  out[1] = l.deviceFrames;
  out[2] = l.safetyFrames;
  out[3] = l.streamFrames;
  out[4] = sr;
  out[5] = e->synth.lastNoteQueueNs.load() / 1e6;
}

static std::vector<AudioOutput::Device> outputs;

int mf1_output_count(void) {
  outputs = AudioOutput::outputDevices();
  return (int)outputs.size();
}

void mf1_output_info(int i, char name[256], char uid[256]) {
  name[0] = uid[0] = 0;
  if (i < 0 || i >= (int)outputs.size()) return;
  snprintf(name, 256, "%s", outputs[i].name.c_str());
  snprintf(uid, 256, "%s", outputs[i].uid.c_str());
}

void mf1_set_output(const char* uid) {
  Engine* e = E();
  LOCK;
  e->outputUID = uid ? uid : "";
  e->restartAudio(e->outputUID);
}
