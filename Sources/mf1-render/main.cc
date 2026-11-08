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

int engineTests();
int iscreamTests();
void iscreamReport(int only, int note, int scoops, bool withToppings);
void iscreamEnvelope(int flavor, int field, int a, int b, int c);
int almanacTests();
void almanacReport(int note);
void almanacWeatherTrace(int sky, int elements);

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

  // Arp notes keep the velocity of the key they come from.
  {
    drainOut();
    mf1_arp_enable(true);
    uint8_t a[] = {0x90, 60, 30}, b[] = {0x90, 64, 110};
    mf1_inject_midi(a, 3);
    mf1_inject_midi(b, 3);
    render((int)(kRate * 0.5));
    bool ok = true;
    int count = 0;
    for (auto m = takeOut(); !m.empty(); m = takeOut())
      if (m[0] == 0x90) {
        count++;
        // Incoming velocity is scaled by 100/127, as on the stock V15.
        ok = ok && m[2] == (m[1] == 60 ? 30 : 110) * 100 / 127;
      }
    uint8_t c[] = {0x80, 60, 0}, d[] = {0x80, 64, 0};
    mf1_inject_midi(c, 3);
    mf1_inject_midi(d, 3);
    mf1_arp_enable(false);
    check(ok && count >= 3, "arp notes keep their own key velocity");
  }

  // Arp gate is measured from when each step was due, not from the block it fired in.
  {
    mf1_all_notes_off();
    render(4800);
    drainOut();
    mf1_arp_enable(true);
    uint8_t on60[] = {0x90, 60, 100}, off60[] = {0x80, 60, 0};
    mf1_inject_midi(on60, 3);
    int t0 = -1;
    int k = 0;
    bool ok = true;
    int offs = 0;
    for (int t = 0; t < (int)kRate; t += 64) {
      render(64);
      for (auto m = takeOut(); !m.empty(); m = takeOut()) {
        if (m[0] == 0x90 && t0 < 0) t0 = t;
        if (m[0] == 0x80 && t0 >= 0) {
          // 1/16 at 120 BPM is 6000 samples; gate 50 releases 3000 after the due time.
          int late = t - (t0 + 6000 * k++);
          ok = ok && late >= 3000 - 64 && late < 3000;
          offs++;
        }
      }
    }
    mf1_inject_midi(off60, 3);
    mf1_arp_enable(false);
    check(ok && offs >= 7, "arp gate counts from the due time of each step");
  }

  // Mono: a long note overtaken by legato notes and chords still ends on time.
  {
    mf1_all_notes_off();
    drainOut();
    mf1_seq_enable(true);
    mf1_seq_set(MF1_S_PATTERN, 4);
    mf1_seq_clear_pattern();
    mf1_seq_set(MF1_S_VOICE, 1);
    mf1_seq_set(MF1_S_STEPS, 4);
    mf1_current_preset();
    mf1_edit_set_mono(true);
    mf1_seq_record(true);
    mf1_key_down(60);
    mf1_seq_select(2);
    mf1_key_up(60);
    mf1_seq_select(-2);
    mf1_key_down(64);
    mf1_key_up(64);
    mf1_key_down(65);
    mf1_key_down(69);
    mf1_key_up(65);
    mf1_key_up(69);
    mf1_seq_record(false);
    render(4800);
    drainOut();
    mf1_seq_play();
    // Halfway through the rest on step 4, every note has ended.
    render(6000 * 3 + 3000);
    uint64_t lo, hi;
    mf1_sounding(&lo, &hi);
    bool quiet = lo == 0 && hi == 0;
    render(6000 * 2);
    mf1_seq_stop();
    render(4800);
    mf1_sounding(&lo, &hi);
    quiet = quiet && lo == 0 && hi == 0;
    mf1_edit_set_mono(false);
    mf1_seq_enable(false);
    check(quiet, "mono long notes end on time and Stop leaves nothing sounding");
  }

  // A long note lasts the sum of the rates of the steps it covers.
  {
    mf1_all_notes_off();
    drainOut();
    mf1_seq_enable(true);
    mf1_seq_set(MF1_S_PATTERN, 5);
    mf1_seq_clear_pattern();
    mf1_seq_set(MF1_S_VOICE, 1);
    mf1_seq_set(MF1_S_STEPS, 4);
    mf1_seq_set(MF1_S_RATE, 2);
    mf1_seq_record(true);
    mf1_seq_set(MF1_S_RATE, 6);
    mf1_key_down(60);
    mf1_seq_select(1);
    mf1_key_up(60);
    mf1_seq_record(false);
    render(4800);
    drainOut();
    mf1_seq_play();
    int on = -1, off = -1;
    for (int t = 0; t < 30000 && off < 0; t += 64) {
      render(64);
      for (auto m = takeOut(); !m.empty(); m = takeOut()) {
        if (m[0] == 0x90 && on < 0) on = t;
        if (m[0] == 0x80 && on >= 0 && off < 0) off = t;
      }
    }
    mf1_seq_stop();
    mf1_seq_enable(false);
    // A 1/16 step then half of a 1/4 step: 6000 + 12000 samples.
    printf("     long note over 1/16 and 1/4 lasted %d samples\n", off - on);
    check(off - on >= 18000 - 64 && off - on < 18000,"long note sums the rates of the steps it covers");
  }

  // Play and MIDI Start load the voice bound to the pattern; a chain loads the
  // next voice only when its first step is due.
  {
    mf1_all_notes_off();
    mf1_seq_enable(true);
    mf1_seq_set(MF1_S_PATTERN, 7);
    mf1_seq_clear_pattern();
    mf1_seq_set(MF1_S_VOICE, 11);
    mf1_seq_set(MF1_S_STEPS, 1);
    mf1_seq_record(true);
    mf1_key_down(60);
    mf1_key_up(60);
    mf1_seq_record(false);
    mf1_seq_set(MF1_S_PATTERN, 8);
    mf1_seq_clear_pattern();
    mf1_seq_set(MF1_S_VOICE, 12);
    mf1_seq_set(MF1_S_STEPS, 1);
    mf1_seq_record(true);
    mf1_key_down(62);
    mf1_key_up(62);
    mf1_seq_record(false);
    mf1_seq_set(MF1_S_PATTERN, 7);
    mf1_seq_set(MF1_S_CHAIN, 2);
    mf1_load_preset(3);
    render(256);
    mf1_seq_play();
    render(3008);
    bool ok = mf1_current_preset() == 11;
    render(6000);
    bool chained = mf1_current_preset() == 12;
    mf1_seq_stop();
    mf1_seq_set(MF1_S_CHAIN, 1);
    mf1_seq_set(MF1_S_SYNC, 1);
    mf1_load_preset(3);
    render(256);
    const uint8_t start[] = {0xFA}, stop[] = {0xFC};
    mf1_inject_midi(start, 1);
    render(256);
    bool synced = mf1_current_preset() == 11;
    mf1_inject_midi(stop, 1);
    render(256);
    mf1_seq_set(MF1_S_SYNC, 0);
    mf1_seq_enable(false);
    render(256);
    check(ok, "Play loads the voice bound to the pattern");
    check(chained, "a chain switches voice when the next pattern starts");
    check(synced, "MIDI Start loads the voice bound to the pattern");
  }

  // MIDI clock: each step lasts its own rate in ticks, swing splits pairs of
  // steps, and gates follow the clock rather than the internal tempo.
  {
    mf1_all_notes_off();
    drainOut();
    mf1_global_set(MF1_G_TEMPO, 60);
    mf1_seq_enable(true);
    mf1_seq_set(MF1_S_PATTERN, 9);
    mf1_seq_clear_pattern();
    mf1_seq_set(MF1_S_VOICE, 1);
    mf1_seq_set(MF1_S_STEPS, 2);
    mf1_seq_set(MF1_S_SYNC, 1);
    mf1_seq_record(true);
    mf1_seq_set(MF1_S_RATE, 4);
    mf1_key_down(60);
    mf1_key_up(60);
    mf1_seq_set(MF1_S_RATE, 6);
    mf1_key_down(62);
    mf1_key_up(62);
    mf1_seq_record(false);
    const uint8_t start[] = {0xFA}, tick[] = {0xF8}, stop[] = {0xFC};
    auto run = [&](std::vector<int>& ons, int& gate) {
      gate = -1;
      int on60 = -1;
      // Prime the tick timing, then start.
      for (int i = 0; i < 8; i++) {
        mf1_inject_midi(tick, 1);
        render(1024);
      }
      drainOut();
      mf1_inject_midi(start, 1);
      for (int i = 0; i < 40; i++) {
        mf1_inject_midi(tick, 1);
        for (int b = 0; b < 16; b++) {
          render(64);
          for (auto m = takeOut(); !m.empty(); m = takeOut()) {
            if (m[0] == 0x90 && b == 0) ons.push_back(i);
            if (m[0] == 0x90 && m[1] == 60 && on60 < 0) on60 = i * 1024 + b * 64;
            if (m[0] == 0x80 && m[1] == 60 && on60 >= 0 && gate < 0) gate = i * 1024 + b * 64 - on60;
          }
        }
      }
      mf1_inject_midi(stop, 1);
      render(4800);
    };
    std::vector<int> ons;
    int gate;
    run(ons, gate);
    printf("     clocked steps at ticks:");
    for (int t : ons) printf(" %d", t);
    printf(", 1/8 gate %d samples\n", gate);
    check(ons.size() >= 5 && ons[0] == 0 && ons[1] == 12 && ons[2] == 18 && ons[3] == 30 && ons[4] == 36,
          "MIDI clock gives each step its own rate");
    // 12 ticks of 1024 samples at gate 50, less the release margin.
    check(gate >= 6144 - 64 && gate < 6144, "clocked gate follows the measured tick rate");
    mf1_seq_set(MF1_S_RATE, 6);
    mf1_seq_set(MF1_S_SWING, 75);
    ons.clear();
    run(ons, gate);
    printf("     swung clocked steps at ticks:");
    for (int t : ons) printf(" %d", t);
    printf("\n");
    check(ons.size() >= 5 && ons[0] == 0 && ons[1] == 9 && ons[2] == 12 && ons[3] == 21 && ons[4] == 24,
          "swing applies under MIDI clock");
    mf1_seq_set(MF1_S_SWING, 50);
    mf1_seq_set(MF1_S_SYNC, 0);
    mf1_seq_enable(false);
    mf1_global_set(MF1_G_TEMPO, 120);
  }

  // Songs: the first chords land on their beats at the song's tempo.
  {
    drainOut();
    mf1_song_play(0);
    std::vector<double> starts;
    for (int block = 0; block < (int)(kRate * 6) / 256; block++) {
      render(256);
      for (auto m = takeOut(); !m.empty(); m = takeOut())
        if (m[0] == 0x90 && m[1] < 56) starts.push_back(block * 256 / kRate);
    }
    mf1_song_stop();
    render(4800);
    drainOut();
    // 124 BPM: the second chord 6 beats after the first, the third 4 later.
    double beat = 60.0 / 124;
    bool ok = starts.size() >= 3 && std::fabs(starts[1] - starts[0] - 6 * beat) < 0.01 &&
              std::fabs(starts[2] - starts[1] - 4 * beat) < 0.01;
    printf("     song chords at:");
    for (double t : starts) printf(" %.3f", t);
    printf("\n");
    check(ok, "song plays its chords on the beat");
  }

  // Faithful mode is exactly mono even when a song asks for width; in
  // extended mode the channels differ but still fold back to the same mono.
  {
    auto renderSong = [](bool extended, std::vector<float>& l, std::vector<float>& r) {
      mf1_global_set(MF1_G_EXTENDED, extended);
      mf1_global_set(MF1_G_STEREO_WIDTH, 100);
      mf1_song_play(0);
      l.assign(48000, 0);
      r.assign(48000, 0);
      for (int i = 0; i < 48000; i += 256) mf1_offline_render_stereo(&l[i], &r[i], 256);
      mf1_song_stop();
      std::vector<float> rest(48000 * 3), rest2(48000 * 3);
      mf1_offline_render_stereo(rest.data(), rest2.data(), 48000 * 3);
      drainOut();
    };
    std::vector<float> fl, fr, el, er;
    renderSong(false, fl, fr);
    bool mono = fl == fr;
    // The scope holds the mono FM-1 signal before MASTER, so the average of
    // the extended channels must equal it times the volume gain.
    mf1_global_set(MF1_G_EXTENDED, 1);
    mf1_global_set(MF1_G_STEREO_WIDTH, 100);
    mf1_song_play(0);
    std::vector<float> l(48000), r(48000), scope(1024);
    for (int i = 0; i < 48000; i += 64) mf1_offline_render_stereo(&l[i], &r[i], 64);
    mf1_scope(scope.data(), 1024);
    mf1_song_stop();
    renderSong(false, el, er);
    mf1_global_set(MF1_G_EXTENDED, 0);
    float vol = mf1_global_get(MF1_G_VOLUME) / 100.f;
    vol *= vol;
    // The scope is written a 64 sample block at a time, so earlier tests can
    // leave it up to a block out of step with the delivered output.
    double diff = 0, fold = 1e9;
    for (int lag = -64; lag <= 64; lag++) {
      double worst = 0;
      for (int i = 64; i < 960; i++) {
        size_t k = l.size() - 1024 + i;
        worst = std::max(worst, (double)std::fabs(0.5f * (l[k] + r[k]) - scope[i + lag] * vol));
      }
      fold = std::min(fold, worst);
    }
    for (size_t k = l.size() - 1024; k < l.size(); k++) diff = std::max(diff, (double)std::fabs(l[k] - r[k]));
    check(mono, "faithful mode output is exactly mono");
    check(diff > 0.01, "extended mode widens a song that asks for it");
    check(fold < 1e-4, "extended stereo folds back to the faithful mono sound");
  }

  // The mode can be switched while a song plays: wide straight away, and
  // back to exactly mono once the width has faded out.
  {
    std::vector<float> l(48000), r(48000);
    auto second = [&] {
      for (int i = 0; i < 48000; i += 64) mf1_offline_render_stereo(&l[i], &r[i], 64);
      double d = 0;
      for (int i = 24000; i < 48000; i++) d = std::max(d, (double)std::fabs(l[i] - r[i]));
      return d;
    };
    mf1_global_set(MF1_G_EXTENDED, 0);
    mf1_song_play(0);
    double a = second();
    mf1_global_set(MF1_G_EXTENDED, 1);
    double b = second();
    mf1_global_set(MF1_G_EXTENDED, 0);
    second();
    double c = second();
    mf1_song_stop();
    render(48000 * 3);
    drainOut();
    check(a == 0 && b > 0.01 && c == 0, "extended mode switches on and off during a song");
  }

  // Bank E exists only in extended mode, and starts with the song patches.
  {
    // Offline, a preset change reaches the synth only when audio renders.
    auto load = [](int i) {
      mf1_load_preset(i);
      render(64);
      return mf1_current_preset();
    };
    mf1_global_set(MF1_G_EXTENDED, 0);
    bool faithful = mf1_preset_count() == 128;
    bool wrapsFaithful = load(128) == 0;  // wraps to 001 when faithful
    mf1_global_set(MF1_G_EXTENDED, 1);
    bool extended = mf1_preset_count() == 160;
    char e1[11];
    mf1_preset_name(128, e1);
    bool reachable = load(128) == 128;
    bool wrapsExtended = load(160) == 0;  // wraps to 001 when extended
    mf1_global_set(MF1_G_EXTENDED, 0);
    check(faithful && wrapsFaithful, "faithful mode has the FM-1's 128 presets");
    check(extended && reachable && wrapsExtended, "extended mode adds bank E, presets 129-160");
    check(strncmp(e1, "RIGHTPLACE", 10) == 0, "bank E starts with the RIGHTPLACE patch at 129");
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

static void writeWav(const char* path, const std::vector<float>& a);

static void wav(int preset, const char* path, int note) {
  mf1_load_preset(preset);
  render(4800);
  uint8_t on[] = {0x90, (uint8_t)note, 100}, off[] = {0x80, (uint8_t)note, 0};
  mf1_inject_midi(on, 3);
  auto a = render((int)kRate * 2);
  mf1_inject_midi(off, 3);
  auto b = render((int)kRate * 2);
  a.insert(a.end(), b.begin(), b.end());
  writeWav(path, a);
}

// Interleaved 16 bit stereo.
static void writeWavStereo(const char* path, const std::vector<float>& l, const std::vector<float>& r) {
  FILE* f = fopen(path, "wb");
  uint32_t dataBytes = (uint32_t)l.size() * 4;
  uint32_t rate = (uint32_t)kRate;
  auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f);
  w32(36 + dataBytes);
  fwrite("WAVEfmt ", 1, 8, f);
  w32(16);
  w16(1);
  w16(2);
  w32(rate);
  w32(rate * 4);
  w16(4);
  w16(16);
  fwrite("data", 1, 4, f);
  w32(dataBytes);
  for (size_t i = 0; i < l.size(); i++)
    for (float x : {l[i], r[i]}) w16((uint16_t)(int16_t)std::lround(std::max(-1.f, std::min(1.f, x)) * 32767));
  fclose(f);
  printf("wrote %s\n", path);
}

static void writeWav(const char* path, const std::vector<float>& a) {
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

// Renders the first seconds of a pre-programmed song with its own voice.
static void song(int index, const char* path, double seconds) {
  mf1_song_play(index);
  writeWav(path, render((int)(kRate * seconds)));
}

// Prints the level of one held note from a song's voice in 50 ms steps,
// to check its envelope: 1.5 s held, then released.
static void songEnvelope(int index) {
  mf1_song_play(index);
  mf1_song_stop();
  render(9600);
  uint8_t on[] = {0x90, 60, 100}, off[] = {0x80, 60, 0};
  mf1_inject_midi(on, 3);
  auto a = render((int)(kRate * 1.5));
  mf1_inject_midi(off, 3);
  auto b = render((int)(kRate * 2.5));
  a.insert(a.end(), b.begin(), b.end());
  int win = (int)(kRate * 0.05);
  float first = 0;
  for (size_t i = 0; i + win <= a.size(); i += win) {
    double sum = 0;
    for (int k = 0; k < win; k++) sum += a[i + k] * a[i + k];
    float rms = (float)std::sqrt(sum / win);
    if (i == 0 || first == 0) first = std::max(first, rms);
    printf("%5.2f s %6.1f dB\n", i / kRate, 20 * std::log10(std::max(1e-6f, rms / first)));
  }
}

// Plays a MIDI file with a built-in song's voice (1-based, 0 = current
// voice), prints the first note-on times and writes the first seconds.
static void midiSong(const char* file, int voiceSong, const char* path, double seconds) {
  auto data = readFile(file);
  drainOut();
  if (!mf1_song_play_midi(data.data(), data.size(), voiceSong - 1)) {
    printf("not a MIDI file\n");
    return;
  }
  std::vector<float> out;
  std::vector<std::pair<double, int>> starts;
  for (int block = 0; block < (int)(kRate * seconds) / 256; block++) {
    auto b = render(256);
    out.insert(out.end(), b.begin(), b.end());
    for (auto m = takeOut(); !m.empty(); m = takeOut())
      if (m[0] == 0x90 && starts.size() < 12) starts.push_back({block * 256 / kRate, m[1]});
  }
  for (auto& [t, n] : starts) printf("%.3f s note %d\n", t, n);
  writeWav(path, out);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: mf1-render <banks dir> test|bench|wav <preset> <out.wav> [note]\n");
    return 2;
  }
  mf1_offline_init(kRate);
  loadFactory(argv[1]);
  std::string mode = argv[2];
  if (mode == "test") {
    int failed = test();
    failed += engineTests();
    failed += iscreamTests();
    failed += almanacTests();
    return failed ? 1 : 0;
  }
  if (mode == "almanac-weather" && argc >= 5) {
    almanacWeatherTrace(atoi(argv[3]), atoi(argv[4]));
    return 0;
  }
  if (mode == "almanac-test") return almanacTests() ? 1 : 0;
  // almanac-report [note]
  if (mode == "almanac-report") {
    almanacReport(argc > 3 ? atoi(argv[3]) : 60);
    return 0;
  }
  // iscream-report [flavor 1-based, 0 for all] [note] [scoops] [toppings 0/1]
  // iscream-env <flavor 1-based> <field> <a> <b> <c>
  if (mode == "iscream-env" && argc >= 8) {
    iscreamEnvelope(atoi(argv[3]) - 1, atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]));
    return 0;
  }
  if (mode == "iscream-report") {
    iscreamReport(argc > 3 ? atoi(argv[3]) - 1 : -1, argc > 4 ? atoi(argv[4]) : 60, argc > 5 ? atoi(argv[5]) : 1,
                  argc > 6 && atoi(argv[6]) != 0);
    return 0;
  }
  if (mode == "bench") bench();
  if (mode == "song" && argc >= 5) song(atoi(argv[3]) - 1, argv[4], argc > 5 ? atof(argv[5]) : 60);
  if (mode == "midi-extended" && argc >= 6) {
    // A stereo render with extended mode on, so a song's width is heard.
    mf1_global_set(MF1_G_EXTENDED, 1);
    auto data = readFile(argv[3]);
    if (!mf1_song_play_midi(data.data(), data.size(), atoi(argv[4]) - 1)) return 1;
    double seconds = argc > 6 ? atof(argv[6]) : 60;
    std::vector<float> l((size_t)(kRate * seconds)), r(l.size());
    for (size_t i = 0; i + 64 <= l.size(); i += 64) mf1_offline_render_stereo(&l[i], &r[i], 64);
    writeWavStereo(argv[5], l, r);
  }
  if (mode == "midi" && argc >= 6) midiSong(argv[3], atoi(argv[4]), argv[5], argc > 6 ? atof(argv[6]) : 60);
  if (mode == "songenv" && argc >= 4) songEnvelope(atoi(argv[3]) - 1);
  if (mode == "wav" && argc >= 5) wav(atoi(argv[3]) - 1, argv[4], argc > 5 ? atoi(argv[5]) : 60);
  return 0;
}
