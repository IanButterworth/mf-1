// Checks for state saving, SysEx output and the control side of the engine.
// Expects mf1_offline_init to have run.

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "MF1Core.h"

int engineTests();

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

std::vector<uint8_t> takeOut() {
  std::vector<uint8_t> buf(65536);
  buf.resize(mf1_take_midi_out(buf.data(), buf.size()));
  return buf;
}

void drainOut() {
  while (!takeOut().empty()) {
  }
}

std::vector<uint8_t> pack7(const std::vector<uint8_t>& in) {
  std::vector<uint8_t> out;
  uint32_t buf = 0;
  int n = 0;
  for (uint8_t v : in) {
    buf |= (uint32_t)v << n;
    n += 8;
    while (n >= 7) {
      out.push_back(buf & 0x7f);
      buf >>= 7;
      n -= 7;
    }
  }
  if (n) out.push_back(buf & 0x7f);
  return out;
}

std::vector<uint8_t> unpack7(const uint8_t* in, size_t len) {
  std::vector<uint8_t> out;
  uint32_t buf = 0;
  int n = 0;
  for (size_t i = 0; i < len; i++) {
    buf |= (uint32_t)(in[i] & 0x7f) << n;
    n += 7;
    while (n >= 8) {
      out.push_back(buf & 0xff);
      buf >>= 8;
      n -= 8;
    }
  }
  return out;
}

std::vector<uint8_t> vendorRequest(uint8_t cmd, const std::vector<uint8_t>& payload) {
  size_t lp = payload.size();
  std::vector<uint8_t> p = {0x00, 0x59, cmd, (uint8_t)lp, (uint8_t)(lp >> 8), (uint8_t)(lp >> 16)};
  unsigned sum = 0;
  for (auto b : payload) sum += b;
  p.insert(p.end(), payload.begin(), payload.end());
  p.push_back((uint8_t)((sum & 0xff) ^ 0xff));
  std::vector<uint8_t> wire = {0xF0};
  auto packed = pack7(p);
  wire.insert(wire.end(), packed.begin(), packed.end());
  wire.push_back(0xF7);
  return wire;
}

std::vector<uint8_t> loopback(const std::vector<uint8_t>& msg, size_t listBytes, int* lists) {
  std::vector<uint8_t> out(msg.size() + 16);
  out.resize(mf1_midi_loopback(msg.data(), msg.size(), listBytes, out.data(), out.size(), lists));
  return out;
}

std::vector<uint8_t> readFile(const std::string& path) {
  std::vector<uint8_t> d;
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return d;
  uint8_t buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + n);
  fclose(f);
  return d;
}

bool writeFile(const std::string& path, const uint8_t* data, size_t len) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, len, f) == len;
  return fclose(f) == 0 && ok;
}

std::vector<std::string> listDir(const std::string& dir) {
  std::vector<std::string> names;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d))
      if (e->d_name[0] != '.') names.push_back(e->d_name);
    closedir(d);
  }
  return names;
}

void removeDir(const std::string& dir) {
  for (auto& n : listDir(dir)) unlink((dir + "/" + n).c_str());
  rmdir(dir.c_str());
}

void midiOutTests() {
  // A whole 32 voice bank dump survives the CoreMIDI encoder and parser.
  std::vector<uint8_t> bank(4104);
  mf1_export_bank(0, bank.data());
  int lists = 0;
  check(loopback(bank, 0, &lists) == bank, "4104 byte bank dump round-trips through the MIDI encoder");
  check(loopback(bank, 1024, &lists) == bank && lists > 1, "bank dump split over small event lists round-trips");

  // The largest vendor read, 16384 bytes, goes out whole and echoes the
  // length actually returned.
  drainOut();
  auto req = vendorRequest(0x23, {0x05, 0, 0, 0, 0, 0xff, 0xff, 0xff});
  mf1_inject_midi(req.data(), req.size());
  auto reply = takeOut();
  auto dec = reply.size() > 2 ? unpack7(reply.data() + 1, reply.size() - 2) : std::vector<uint8_t>();
  size_t lp = dec.size() >= 6 ? dec[3] | dec[4] << 8 | dec[5] << 16 : 0;
  size_t echoed = dec.size() >= 14 ? dec[11] | dec[12] << 8 | dec[13] << 16 : 0;
  check(echoed == 16384 && lp == 8 + 16384, "oversized vendor read echoes the 16384 bytes it returns");
  check(reply.size() == 18744, "16384 byte vendor read is 18744 bytes on the wire");
  check(loopback(reply, 0, &lists) == reply, "18744 byte vendor read round-trips through the MIDI encoder");

  // Input SysEx beyond 64 KB is dropped rather than buffered.
  std::vector<uint8_t> huge(70000, 0x11);
  huge.front() = 0xF0;
  huge.back() = 0xF7;
  check(loopback(huge, 0, &lists).empty(), "SysEx over 64 KB is dropped on input");
}

void controlTests() {
  // Address 155 is past the hardware's buffer, so it must not mute operators.
  mf1_load_preset(0);
  int enable = mf1_edit_get(155);
  const uint8_t pc[] = {0xF0, 0x43, 0x10, 0x01, 0x1B, 0x00, 0xF7};
  mf1_inject_midi(pc, sizeof(pc));
  check(mf1_edit_get(155) == enable && !mf1_dirty(), "parameter change to address 155 is ignored");

  uint32_t g = mf1_generation();
  mf1_rename("RENAMED   ");
  check(mf1_generation() != g, "rename bumps the generation");
  g = mf1_generation();
  mf1_copy_voice(1);
  check(mf1_generation() != g, "copy voice bumps the generation");
  mf1_load_preset(0);
  g = mf1_generation();
  mf1_save();
  check(mf1_generation() != g, "save bumps the generation");
  std::vector<uint8_t> vmem(4096, 0);
  g = mf1_generation();
  check(mf1_import_syx(vmem.data(), vmem.size()) == 1 && mf1_generation() != g, "raw VMEM import bumps the generation");
  mf1_bank_cancel();
}

void stateTests() {
  const char* base = getenv("TMPDIR");
  std::string tmpl = std::string(base && *base ? base : "/tmp") + "/mf1-state-XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back(0);
  if (!mkdtemp(buf.data())) {
    check(false, "create a temporary state directory");
    return;
  }
  std::string dir = buf.data();
  std::string path = dir + "/state.bin";

  check(!mf1_offline_open_state(dir.c_str()), "an empty directory has no saved state");
  mf1_save();
  auto saved = readFile(path);
  check(saved.size() > 12, "save writes state.bin");
  check(mf1_offline_open_state(dir.c_str()), "saved state loads back");

  // A save that cannot be written leaves the last good file alone.
  chmod(dir.c_str(), 0555);
  mf1_global_set(MF1_G_VELOCITY, mf1_global_get(MF1_G_VELOCITY));
  chmod(dir.c_str(), 0755);
  check(readFile(path) == saved && listDir(dir).size() == 1, "failed save keeps the previous state.bin");

  // A truncated file is kept aside and does not ask for factory banks.
  std::vector<uint8_t> cut(saved.begin(), saved.begin() + saved.size() / 2);
  writeFile(path, cut.data(), cut.size());
  check(!mf1_offline_open_state(dir.c_str()), "truncated state does not load");
  check(!mf1_needs_factory(), "truncated state does not trigger a factory import");
  std::string aside;
  for (auto& n : listDir(dir))
    if (n.rfind("state.bin.unreadable-", 0) == 0) aside = n;
  check(!aside.empty() && readFile(dir + "/" + aside) == cut, "truncated state is kept byte for byte");
  check(readFile(path).empty(), "truncated state.bin is moved out of the way");

  // So is a file from another version.
  std::vector<uint8_t> other = saved;
  other[4] = 99;
  writeFile(path, other.data(), other.size());
  check(!mf1_offline_open_state(dir.c_str()) && listDir(dir).size() == 2, "state of another version is kept aside");

  char msg[64];
  mf1_take_message(msg);
  mf1_offline_open_state("");
  removeDir(dir);
}

}  // namespace

int engineTests() {
  failures = 0;
  midiOutTests();
  controlTests();
  stateTests();
  return failures;
}
