// Headless checks and benchmarks for the MF-1 engine.
//   mf1-render <banks dir> test          protocol and engine checks
//   mf1-render <banks dir> bench         CPU cost at full polyphony
//   mf1-render <banks dir> wav <preset> <out.wav> [note]

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "MF1Core.h"

static const double kRate = 48000;
static int failures = 0;

static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

static std::vector<uint8_t> readFile(const std::string& path) {
  std::vector<uint8_t> d;
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return d;
  uint8_t buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + n);
  fclose(f);
  return d;
}

static void loadFactory(const std::string& dir) {
  for (int b = 0; b < 4; b++) {
    auto d = readFile(dir + "/FM-1_factory_bank" + std::to_string(b + 1) + ".syx");
    if (!mf1_import_factory_bank(b, d.data(), d.size())) printf("could not load bank %d\n", b + 1);
  }
}

static std::vector<float> render(int frames) {
  std::vector<float> out(frames);
  for (int i = 0; i < frames; i += 256) mf1_offline_render(out.data() + i, std::min(256, frames - i));
  return out;
}

static float peakOf(const std::vector<float>& v) {
  float p = 0;
  for (float x : v) p = std::max(p, std::fabs(x));
  return p;
}

static std::vector<uint8_t> takeOut() {
  std::vector<uint8_t> buf(20000);
  size_t n = mf1_take_midi_out(buf.data(), buf.size());
  buf.resize(n);
  return buf;
}

static void drainOut() {
  while (!takeOut().empty()) {
  }
}

static int test() {
  // Identify request captured from the M-VAVE updater must be answered with
  // the packed identity record.
  const uint8_t identify[] = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
  drainOut();
  mf1_inject_midi(identify, sizeof(identify));
  auto reply = takeOut();
  check(reply.size() == 41, "identity reply is 41 bytes on the wire");
  check(reply.size() > 8 && reply[1] == 0x00 && reply[2] == 0x32 && reply[3] == 0x45, "identity reply header 00 32 45");

  // Setup message for bank 2 from the capture; the device acks it.
  const uint8_t setup[] = {0xF0, 0x00, 0x32, 0x05, 0x29, 0x00, 0x00, 0x40, 0x02, 0x00, 0x20, 0x00, 0x00, 0x20, 0x1D, 0xF7};
  mf1_inject_midi(setup, sizeof(setup));
  auto ack = takeOut();
  const uint8_t expectAck[] = {0xF0, 0x00, 0x32, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x01, 0xF7};
  check(ack.size() == sizeof(expectAck) && memcmp(ack.data(), expectAck, sizeof(expectAck)) == 0,
        "ack matches the captured device ack byte for byte");

  // DX7 parameter change: OP1 output level (address 121) to 50.
  mf1_load_preset(0);
  int orig = mf1_edit_get(121);
  uint8_t level = (uint8_t)(orig == 50 ? 51 : 50);
  const uint8_t pc[] = {0xF0, 0x43, 0x10, 0x00, 121, level, 0xF7};
  mf1_inject_midi(pc, sizeof(pc));
  check(mf1_edit_get(121) == level, "parameter change F0 43 10 writes the edit buffer");
  check(mf1_dirty(), "parameter change marks the voice as edited");
  mf1_load_preset(0);
  check(!mf1_dirty() && mf1_edit_get(121) == orig, "program change drops unsaved edits");

  // Vendor read of the edit buffer returns the 155 byte VCED.
  {
    std::vector<uint8_t> payload = {0x05, 0, 0, 0, 0, 155, 0, 0};
    // Build the request like czietz's mkcmd.
    std::vector<uint8_t> p = {0x00, 0x59, 0x23, 8, 0, 0};
    unsigned sum = 0;
    for (auto b : payload) sum += b;
    p.insert(p.end(), payload.begin(), payload.end());
    p.push_back((uint8_t)((sum & 0xff) ^ 0xff));
    std::vector<uint8_t> wire = {0xF0};
    uint32_t buf = 0;
    int n = 0;
    for (auto v : p) {
      buf |= (uint32_t)v << n;
      n += 8;
      while (n >= 7) {
        wire.push_back(buf & 0x7f);
        buf >>= 7;
        n -= 7;
      }
    }
    if (n) wire.push_back(buf & 0x7f);
    wire.push_back(0xF7);
    mf1_inject_midi(wire.data(), wire.size());
    auto r = takeOut();
    // Unpack and compare the voice name.
    std::vector<uint8_t> dec;
    buf = 0;
    n = 0;
    for (size_t i = 1; i + 1 < r.size(); i++) {
      buf |= (uint32_t)(r[i] & 0x7f) << n;
      n += 7;
      while (n >= 8) {
        dec.push_back(buf & 0xff);
        buf >>= 8;
        n -= 8;
      }
    }
    char name[11];
    mf1_edit_name(name);
    bool ok = dec.size() >= 14 + 155 && memcmp(&dec[14 + 145], name, 10) == 0;
    check(ok, "vendor read (cmd 0x23) returns the edit buffer VCED");
  }

  // Exported bank re-imports to the same names.
  {
    std::vector<uint8_t> bank(4104);
    mf1_export_bank(3, bank.data());
    check(mf1_import_syx(bank.data(), bank.size()) == 1, "exported bank parses as a 32 voice dump");
    char a[11], b[11];
    mf1_bank_pending_name(31, a);
    mf1_preset_name(127, b);
    check(strcmp(a, b) == 0, "bank round trip keeps voice names");
    mf1_bank_cancel();
  }

  // Arpeggiator: Up over three held notes plays them in rising order.
  {
    drainOut();
    mf1_load_preset(1);
    mf1_arp_set(MF1_A_MODE, 0);
    mf1_arp_set(MF1_A_RATE, 6);
    mf1_global_set(MF1_G_TEMPO, 120);
    mf1_arp_enable(true);
    for (int n : {64, 60, 67}) {
      uint8_t m[] = {0x90, (uint8_t)n, 100};
      mf1_inject_midi(m, 3);
    }
    render((int)(kRate * 1.0));
    std::vector<int> ons;
    for (auto m = takeOut(); !m.empty(); m = takeOut())
      if (m[0] == 0x90) ons.push_back(m[1]);
    for (int n : {64, 60, 67}) {
      uint8_t m[] = {0x80, (uint8_t)n, 0};
      mf1_inject_midi(m, 3);
    }
    mf1_arp_enable(false);
    // 1/16 at 120 BPM is 125 ms: 8 steps in one second.
    bool ok = ons.size() >= 7 && ons.size() <= 9;
    for (size_t i = 0; ok && i < ons.size(); i++) ok = ons[i] == (int[]){60, 64, 67}[i % 3];
    printf("     arp up played %zu notes:", ons.size());
    for (int n : ons) printf(" %d", n);
    printf("\n");
    check(ok, "arp Up plays held notes low to high at 1/16, 120 BPM");
  }

  // A key held when ARP is switched on still releases.
  {
    mf1_all_notes_off();
    render(4800);
    mf1_key_down(62);
    render(4800);
    mf1_arp_enable(true);
    mf1_key_up(62);
    render(4800);
    uint64_t lo, hi;
    mf1_sounding(&lo, &hi);
    mf1_arp_enable(false);
    check(lo == 0 && hi == 0, "key held across ARP on releases");
  }

  // Swing 75 with gate 100: each arp note ends before the next starts.
  {
    drainOut();
    mf1_arp_set(MF1_A_SWING, 75);
    mf1_arp_set(MF1_A_GATE, 100);
    mf1_arp_enable(true);
    uint8_t on[] = {0x90, 60, 100}, off[] = {0x80, 60, 0};
    mf1_inject_midi(on, 3);
    render((int)kRate);
    mf1_inject_midi(off, 3);
    mf1_arp_enable(false);
    mf1_arp_set(MF1_A_SWING, 50);
    mf1_arp_set(MF1_A_GATE, 50);
    std::vector<int> st;
    for (auto m = takeOut(); !m.empty(); m = takeOut()) st.push_back(m[0] & 0xf0);
    bool alternate = st.size() >= 10;
    for (size_t i = 0; alternate && i + 1 < st.size(); i++) alternate = st[i] != st[i + 1];
    check(alternate, "swung, full-gate arp notes never overlap");
  }

  // Sequencer: step-record two notes and a chord, then play them back.
  {
    drainOut();
    mf1_seq_enable(true);
    mf1_seq_set(MF1_S_PATTERN, 3);
    mf1_seq_clear_pattern();
    mf1_seq_set(MF1_S_STEPS, 4);
    mf1_seq_record(true);
    auto tap = [](std::initializer_list<int> notes) {
      for (int n : notes) mf1_key_down(n);
      for (int n : notes) mf1_key_up(n);
    };
    tap({60});
    tap({62});
    tap({64, 67});
    mf1_seq_record(false);
    check(mf1_seq_step_mask() == 0x7, "three steps recorded, fourth left as a rest");
    render(4800);
    drainOut();
    mf1_seq_play();
    render((int)(kRate * 0.5));
    std::vector<int> ons;
    for (auto m = takeOut(); !m.empty(); m = takeOut())
      if (m[0] == 0x90) ons.push_back(m[1]);
    mf1_seq_stop();
    mf1_seq_enable(false);
    printf("     seq played:");
    for (int n : ons) printf(" %d", n);
    printf("\n");
    bool ok = ons.size() >= 4 && ons[0] == 60 && ons[1] == 62 &&
              ((ons[2] == 64 && ons[3] == 67) || (ons[2] == 67 && ons[3] == 64));
    check(ok, "sequencer plays recorded steps and the chord in order");
  }

  char name[11];
  mf1_preset_name(0, name);
  printf("     preset 001 is '%s'\n", name);
  mf1_preset_name(127, name);
  printf("     preset 128 is '%s'\n", name);

  // Engine: a note sounds, and is silent again after release.
  mf1_load_preset(0);
  render(4800);
  const uint8_t on[] = {0x90, 60, 100}, off[] = {0x80, 60, 0};
  mf1_inject_midi(on, 3);
  float p1 = peakOf(render(24000));
  mf1_inject_midi(off, 3);
  render(48000 * 4);
  float p2 = peakOf(render(4800));
  printf("     PIANO 1 C4 peak %.3f, after release %.5f\n", p1, p2);
  check(p1 > 0.05f && p1 <= 1.0f, "note produces sound below full scale");
  check(p2 < 0.001f, "note decays to silence after release");

  // Loudest factory preset with a 12 note chord stays below clipping often enough to report.
  int clipped = 0;
  float loudest = 0;
  int loudestIx = 0;
  for (int pr = 0; pr < 128; pr++) {
    mf1_load_preset(pr);
    mf1_all_notes_off();
    render(48000);
    mf1_inject_midi(on, 3);
    float p = peakOf(render(12000));
    mf1_inject_midi(off, 3);
    if (p > loudest) {
      loudest = p;
      loudestIx = pr;
    }
    if (p >= 0.999f) clipped++;
  }
  printf("     single note: loudest preset %03d peak %.3f, %d of 128 at full scale\n", loudestIx + 1, loudest, clipped);
  check(clipped == 0, "no factory preset clips on a single note");
  return failures;
}

static void bench() {
  // ORGAN 1 sustains, so all six operators of all voices keep running.
  mf1_load_preset(1);
  // 12 notes held, plus every effect on: the worst case.
  for (int cc = 0; cc < 24; cc += 4) mf1_fx_set(cc, 1);
  for (int n = 0; n < 12; n++) {
    uint8_t on[] = {0x90, (uint8_t)(48 + n * 2), 100};
    mf1_inject_midi(on, 3);
  }
  const int seconds = 20;
  auto t0 = std::chrono::steady_clock::now();
  render((int)kRate * seconds);
  double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("12 voices + all FX: %.1f s of audio in %.3f s, %.1fx realtime, %.2f%% of one core\n", (double)seconds, dt,
         seconds / dt, 100.0 * dt / seconds);
  for (int cc = 0; cc < 24; cc += 4) mf1_fx_set(cc, 0);
  t0 = std::chrono::steady_clock::now();
  render((int)kRate * seconds);
  dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("12 voices, FX off:  %.1fx realtime, %.2f%% of one core\n", seconds / dt, 100.0 * dt / seconds);
}

static void wav(int preset, const char* path, int note) {
  mf1_load_preset(preset);
  render(4800);
  uint8_t on[] = {0x90, (uint8_t)note, 100}, off[] = {0x80, (uint8_t)note, 0};
  mf1_inject_midi(on, 3);
  auto a = render((int)kRate * 2);
  mf1_inject_midi(off, 3);
  auto b = render((int)kRate * 2);
  a.insert(a.end(), b.begin(), b.end());
  FILE* f = fopen(path, "wb");
  uint32_t dataBytes = (uint32_t)a.size() * 2;
  uint32_t rate = (uint32_t)kRate;
  auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f);
  w32(36 + dataBytes);
  fwrite("WAVEfmt ", 1, 8, f);
  w32(16);
  w16(1);
  w16(1);
  w32(rate);
  w32(rate * 2);
  w16(2);
  w16(16);
  fwrite("data", 1, 4, f);
  w32(dataBytes);
  for (float x : a) w16((uint16_t)(int16_t)std::lround(std::max(-1.f, std::min(1.f, x)) * 32767));
  fclose(f);
  printf("wrote %s\n", path);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: mf1-render <banks dir> test|bench|wav <preset> <out.wav> [note]\n");
    return 2;
  }
  mf1_offline_init(kRate);
  loadFactory(argv[1]);
  std::string mode = argv[2];
  if (mode == "test") return test() ? 1 : 0;
  if (mode == "bench") bench();
  if (mode == "wav" && argc >= 5) wav(atoi(argv[3]) - 1, argv[4], argc > 5 ? atoi(argv[5]) : 60);
  return 0;
}
