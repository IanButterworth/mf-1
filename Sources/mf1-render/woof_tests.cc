// Checks and recordings for the Woof dogs, through the offline engine.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "MF1Core.h"
#include "WoofCore.h"

namespace {

const double kRate = 48000;
int failures = 0;

void check(bool ok, const std::string& what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) failures++;
}

std::vector<float> render(double seconds) {
  std::vector<float> out((size_t)(kRate * seconds));
  for (size_t i = 0; i < out.size(); i += 64) mf1_offline_render(out.data() + i, (int)std::min<size_t>(64, out.size() - i));
  return out;
}

double db(double x) { return 20 * std::log10(std::max(1e-9, x)); }

double rms(const std::vector<float>& a, double from, double to) {
  size_t i0 = (size_t)(std::max(0.0, from) * kRate), i1 = std::min(a.size(), (size_t)(to * kRate));
  double sum = 0;
  for (size_t i = i0; i < i1; i++) sum += (double)a[i] * a[i];
  return std::sqrt(sum / std::max<size_t>(1, i1 - std::min(i0, i1)));
}

bool finite(const std::vector<float>& a) {
  for (float x : a)
    if (!std::isfinite(x)) return false;
  return true;
}

void silence() {
  mf1_all_notes_off();
  for (int i = 0; i < 300; i++)
    if (i >= 3 && rms(render(0.1), 0, 0.1) < 1e-6) break;
}

// Offline, the engine drops commands once its queue is full, so render a little after the 128 bank presets.
void install() {
  woof_install();
  render(0.05);
}

std::string breed(int i) {
  WoofDog d;
  woof_dog(i, &d);
  return d.breed;
}

// A dog's bark on its own: single barks, no room or echo.
void bare(int dog, int32_t s[WF_FIELDS]) {
  woof_default_dog(dog, s);
  s[WF_DISTANCE] = 0, s[WF_ECHO] = 0, s[WF_PLAY] = WF_SINGLE;
}

// Presses the notes one key gives for `hold` seconds, then lets go for `tail`.
std::vector<float> play(const int32_t* dog, int note, double hold = 0.6, double tail = 0.6) {
  mf1_global_set(MF1_G_QUIRKS, 1);
  woof_load(dog);
  silence();
  render(0.05);
  int notes[8];
  int n = woof_key_notes(dog, note, notes);
  for (int i = 0; i < n; i++) mf1_key_down(notes[i]);
  auto a = render(hold);
  for (int i = 0; i < n; i++) mf1_key_up(notes[i]);
  auto b = render(tail);
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// How long the level stays within 20 dB of its loudest point, and when that point comes.
void shape(const std::vector<float>& a, double upTo, double* length, double* peakAt) {
  double top = 0, at = 0;
  for (double t = 0; t + 0.01 <= upTo; t += 0.005) {
    double r = rms(a, t, t + 0.01);
    if (r > top) top = r, at = t;
  }
  double first = -1, last = 0;
  for (double t = 0; t + 0.01 <= upTo; t += 0.005)
    if (rms(a, t, t + 0.01) > top * 0.1) {
      if (first < 0) first = t;
      last = t + 0.01;
    }
  *length = last - std::max(0.0, first);
  *peakAt = at - std::max(0.0, first);
}

// The strongest period between 70 Hz and 1.2 kHz in a 20 ms window, by autocorrelation, as a frequency.
double pitchAt(const std::vector<float>& a, double t) {
  size_t i0 = (size_t)(t * kRate), n = (size_t)(0.02 * kRate);
  if (i0 + n + (size_t)(kRate / 70) >= a.size()) return 0;
  double best = -1;
  int bestLag = 0;
  for (int lag = (int)(kRate / 1200); lag < (int)(kRate / 70); lag++) {
    double num = 0, e0 = 0, e1 = 0;
    for (size_t i = 0; i < n; i++) {
      num += (double)a[i0 + i] * a[i0 + i + lag];
      e0 += (double)a[i0 + i] * a[i0 + i];
      e1 += (double)a[i0 + i + lag] * a[i0 + i + lag];
    }
    double r = num / std::sqrt(e0 * e1 + 1e-20);
    if (r > best + 0.02) best = r, bestLag = lag;
  }
  return bestLag ? kRate / bestLag : 0;
}

void writeWav(const char* path, const std::vector<float>& a) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  uint32_t dataBytes = (uint32_t)a.size() * 2, rate = (uint32_t)kRate;
  auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f);
  w32(36 + dataBytes);
  fwrite("WAVEfmt ", 1, 8, f);
  w32(16), w16(1), w16(1), w32(rate), w32(rate * 2), w16(2), w16(16);
  fwrite("data", 1, 4, f);
  w32(dataBytes);
  for (float x : a) w16((uint16_t)(int16_t)std::lround(std::max(-1.f, std::min(1.f, x)) * 32767));
  fclose(f);
}

}  // namespace

// Three barks of a dog, each key held a quarter of a second, with its place's room and echo unless `dry`.
// Settings after the note are field=value pairs, by field number.
void woofWav(int dog, const char* path, int note, int argc, char** argv) {
  install();
  int32_t s[WF_FIELDS];
  woof_default_dog(dog, s);
  s[WF_PLAY] = WF_SINGLE;
  for (int i = 0; i < argc; i++) {
    int f, v;
    if (sscanf(argv[i], "%d=%d", &f, &v) == 2 && f >= 0 && f < WF_FIELDS) s[f] = v;
  }
  std::vector<float> out;
  for (int k = 0; k < 3; k++) {
    auto a = play(s, note, 0.25, 0.45);
    out.insert(out.end(), a.begin(), a.end());
  }
  writeWav(path, out);
  printf("wrote %s\n", path);
}

void woofReport(int note) {
  install();
  printf("note %d, dry single barks | loudest dB | length and peak (ms, within 20 dB) | pitch Hz at 10, 40, 80, 140 ms\n", note);
  for (int i = 0; i < woof_dog_count(); i++) {
    int32_t s[WF_FIELDS];
    bare(i, s);
    auto a = play(s, note, 0.25, 0.6);
    double len, at;
    shape(a, 0.8, &len, &at);
    double top = 0;
    for (double t = 0; t < 0.8; t += 0.01) top = std::max(top, rms(a, t, t + 0.02));
    double start = 0;
    for (double t = 0; t < 0.3; t += 0.001)
      if (rms(a, t, t + 0.002) > top * 0.1) {
        start = t;
        break;
      }
    printf("%-11s %6.1f | %4.0f %4.0f | %5.0f %5.0f %5.0f %5.0f\n", breed(i).c_str(), db(top), len * 1000, at * 1000,
           pitchAt(a, start + 0.01), pitchAt(a, start + 0.04), pitchAt(a, start + 0.08), pitchAt(a, start + 0.14));
  }
}

namespace {

// Times the level rises above a fraction of the loudest point after being below it: bark starts.
int onsets(const std::vector<float>& a, double upTo) {
  std::vector<double> env;
  for (double t = 0; t + 0.01 <= upTo; t += 0.01) env.push_back(rms(a, t, t + 0.01));
  double top = *std::max_element(env.begin(), env.end());
  int n = 0;
  bool high = false;
  for (double e : env) {
    if (!high && e > top * 0.5) n++, high = true;
    if (high && e < top * 0.2) high = false;
  }
  return n;
}

// Zero crossings per second, a rough brightness.
double crossings(const std::vector<float>& a, double from, double to) {
  size_t i0 = (size_t)(from * kRate), i1 = std::min(a.size(), (size_t)(to * kRate));
  int n = 0;
  for (size_t i = i0 + 1; i < i1; i++) n += (a[i - 1] < 0) != (a[i] < 0);
  return n / std::max(1e-9, (double)(i1 - i0) / kRate);
}

// One stack of the bark alone, the others muted (bit i mutes OP(i+1)).
std::vector<float> stack(const int32_t* dog, int mutes, int note, double hold = 0.4) {
  mf1_global_set(MF1_G_QUIRKS, 1);
  woof_load(dog);
  silence();
  mf1_set_op_mutes(mutes);
  render(0.05);
  mf1_key_down(note);
  auto a = render(hold);
  mf1_key_up(note);
  mf1_set_op_mutes(0);
  return a;
}

double semitones(double a, double b) { return 12 * std::log2(std::max(1e-9, b) / std::max(1e-9, a)); }

}  // namespace

int woofTests() {
  failures = 0;
  mf1_global_set(MF1_G_QUIRKS, 1);
  install();
  const int n = woof_dog_count();
  check(n == 13, "there are thirteen dogs");

  {
    bool names = true, ranges = true, voices = true;
    int styles[WF_PLAY_COUNT] = {0};
    for (int i = 0; i < n; i++) {
      WoofDog d;
      names &= woof_dog(i, &d) && strlen(d.name) > 0 && strlen(d.breed) > 0 && strlen(d.word) > 0 && strlen(d.blurb) > 0;
      int32_t s[WF_FIELDS], c[WF_FIELDS];
      woof_default_dog(i, s);
      memcpy(c, s, sizeof(s));
      woof_clamp(c);
      ranges &= memcmp(s, c, sizeof(s)) == 0 && s[WF_DOG] == i;
      styles[s[WF_PLAY]]++;
      woof_load(s);
      uint8_t v[155];
      woof_voice(s, v);
      for (int k = 0; k < 145; k++) voices &= mf1_edit_get(k) == v[k];
    }
    check(names, "every dog has a voice name, a breed, a word and a few words more");
    check(ranges, "every dog's settings are in range and name the dog");
    bool all = true;
    for (int k = 0; k < WF_PLAY_COUNT; k++) all &= styles[k] >= 1;
    check(all, "the dogs as bred use every playing style");
    check(voices, "a loaded dog's edit buffer is exactly its bark");
  }

  {
    uint8_t dump[4104];
    woof_export_bank(dump);
    int sum = 0;
    for (int i = 6; i < 6 + 4096; i++) sum += dump[i];
    check(dump[0] == 0xF0 && dump[3] == 0x09 && dump[4103] == 0xF7 && ((sum + dump[6 + 4096]) & 0x7f) == 0,
          "the dog bank is a valid DX7 32-voice dump");
  }

  // Every dog barks, at about the same loudness, and a plain bark is the length of a real one (about a tenth
  // to a third of a second in recordings) and stops by itself while the key is still held.
  {
    double lo = 1e9, hi = -1e9;
    bool ok = true, length = true, stops = true;
    for (int i = 0; i < n; i++) {
      int32_t s[WF_FIELDS];
      bare(i, s);
      s[WF_HOWL] = 0, s[WF_GROWL] = 0;
      auto a = play(s, 66, 1.0, 0.3);
      ok &= finite(a);
      double top = 0;
      for (double t = 0; t < 0.6; t += 0.01) top = std::max(top, rms(a, t, t + 0.02));
      lo = std::min(lo, db(top)), hi = std::max(hi, db(top));
      double len, at;
      shape(a, 0.9, &len, &at);
      if (len < 0.08 || len > 0.45 || at > 0.1) {
        length = false;
        printf("     %s: %.0f ms long, loudest at %.0f ms\n", breed(i).c_str(), len * 1000, at * 1000);
      }
      if (db(rms(a, 0.75, 0.95)) >= db(top) - 40) {
        stops = false;
        printf("     %s: %.1f dB below its loudest after 0.75 s\n", breed(i).c_str(), db(top) - db(rms(a, 0.75, 0.95)));
      }
    }
    char what[120];
    snprintf(what, sizeof(what), "every dog barks, within 9 dB of the others (%.1f to %.1f dB)", lo, hi);
    check(ok && lo > -45 && hi - lo <= 9, what);
    check(length, "a bark lasts 80 to 450 ms and is loudest within its first 100 ms");
    check(stops, "a bark stops by itself while the key is held");
  }

  // The throat's pitch jumps up as the bark opens and falls away after it, as a real bark's does.
  {
    bool shapeOk = true;
    for (int i = 0; i < n; i++) {
      int32_t s[WF_FIELDS];
      bare(i, s);
      s[WF_HOWL] = 0, s[WF_GROWL] = 0;
      auto a = stack(s, 0x3c, 62);
      double top = 0;
      for (double t = 0.02; t <= 0.1; t += 0.01) top = std::max(top, pitchAt(a, t));
      double up = semitones(pitchAt(a, 0.006), top), down = semitones(top, pitchAt(a, 0.2));
      if (up < 2.5 || down > -1) {
        shapeOk = false;
        printf("     %s: up %.1f, then %.1f semitones\n", breed(i).c_str(), up, down);
      }
    }
    check(shapeOk, "every bark's pitch rises by 2.5 semitones or more, then falls");
  }

  // The mouth is a fixed formant: an octave up, the throat's brightness doubles but the mouth's hardly moves.
  {
    int32_t s[WF_FIELDS];
    bare(0, s);
    double t1 = crossings(stack(s, 0x3c, 54), 0.06, 0.2), t2 = crossings(stack(s, 0x3c, 66), 0.06, 0.2);
    double m1 = crossings(stack(s, 0x33, 54), 0.06, 0.2), m2 = crossings(stack(s, 0x33, 66), 0.06, 0.2);
    char what[140];
    snprintf(what, sizeof(what), "the throat follows the key and the mouth stays put (an octave up: %.2fx and %.2fx)",
             t2 / t1, m2 / m1);
    check(t2 / t1 > 1.7 && m2 / m1 < 1.35, what);
  }

  // A small dog barks higher than a big one; Size moves the same dog by up to an octave.
  {
    int32_t s[WF_FIELDS];
    bare(2, s);
    double tiny = pitchAt(stack(s, 0x3c, 62), 0.07);
    bare(10, s);
    double giant = pitchAt(stack(s, 0x3c, 62), 0.07);
    bare(0, s);
    s[WF_SIZE] = 0;
    double small = pitchAt(stack(s, 0x3c, 62), 0.07);
    s[WF_SIZE] = 100;
    double big = pitchAt(stack(s, 0x3c, 62), 0.07);
    check(tiny > 3 * giant && small > 1.7 * big, "a chihuahua barks far higher than a great dane, and Size lowers a dog");
  }

  // Howl and Growl carry on while the key is held; Howl rises more slowly.
  {
    int32_t s[WF_FIELDS];
    bare(0, s);
    s[WF_HOWL] = 100;
    auto howl = play(s, 62, 1.5, 0.3);
    s[WF_HOWL] = 0, s[WF_GROWL] = 100;
    auto growl = play(s, 62, 1.5, 0.3);
    double h = 0, g = 0;
    for (double t = 0; t < 0.5; t += 0.01) h = std::max(h, rms(howl, t, t + 0.02)), g = std::max(g, rms(growl, t, t + 0.02));
    check(db(rms(howl, 1.2, 1.4)) > db(h) - 12, "a howl holds on while the key is held");
    check(db(rms(growl, 1.2, 1.4)) > db(g) - 24, "a growl carries on under a held key");
    double lenH, atH, lenB, atB;
    shape(howl, 1.5, &lenH, &atH);
    bare(0, s);
    shape(play(s, 62, 1.0, 0.3), 1.0, &lenB, &atB);
    check(atH > atB + 0.05, "a howl swells in more slowly than a bark");
  }

  // Playing styles.
  {
    int32_t s[WF_FIELDS];
    bare(0, s);
    int notes[8];
    check(woof_key_notes(s, 60, notes) == 1 && notes[0] == 60, "single plays the key");
    s[WF_PLAY] = WF_CHORUS;
    int sizes[3];
    for (int k = 0; k < 3; k++) s[WF_PACK] = k, sizes[k] = woof_key_notes(s, 60, notes);
    check(sizes[0] == 2 && sizes[1] == 3 && sizes[2] == 5 && notes[0] == 60, "a chorus is a pair, a trio or a litter of five");
    s[WF_PLAY] = WF_REPEAT, s[WF_TEMPO] = 120, s[WF_STEP] = 1;
    auto a = play(s, 62, 2.0, 0.1);
    check(mf1_arp_on(), "repeat switches the arpeggiator on");
    int hits = onsets(a, 2.0);
    char what[80];
    snprintf(what, sizeof(what), "a held key barks again every eighth note at 120 BPM (%d barks in 2 s)", hits);
    check(hits >= 6 && hits <= 9, what);
    s[WF_PLAY] = WF_SINGLE;
    woof_apply(s);
    check(!mf1_arp_on(), "single barks switch it off again");
  }

  // The scene draws every dog in every place without leaving a pixel unset.
  {
    bool ok = true;
    std::vector<uint32_t> px(WF_SCENE_W * WF_SCENE_H);
    for (int i = 0; i < n; i++)
      for (int p = 0; p < WF_PLACE_COUNT; p++)
        for (int play = 0; play < WF_PLAY_COUNT; play++) {
          int32_t s[WF_FIELDS];
          woof_default_dog(i, s);
          s[WF_PLACE] = p, s[WF_PLAY] = play, s[WF_PACK] = WF_LITTER;
          std::fill(px.begin(), px.end(), 0xFF000000u);
          woof_scene(s, i + p, play & 1 ? 100 : 0, play == 3 ? 4 : -1, px.data());
          for (uint32_t c : px) ok &= c <= 0xFFFFFFu;
        }
    check(ok, "the scene paints every pixel for every dog, place and style");
  }

  silence();
  return failures;
}

// Every dog's scene, four across, as raw RGB at four times the size: `woof-scene <out.rgb>
// [place] [bark 0/1] [step]`. The size of the sheet is printed.
void woofSceneSheet(const char* path, int place, int bark, int step) {
  const int k = 4, w = WF_SCENE_W * k, h = WF_SCENE_H * k, rows = (woof_dog_count() + 3) / 4;
  std::vector<uint8_t> rgb((size_t)w * 4 * h * rows * 3, 0xFF);
  std::vector<uint32_t> px(WF_SCENE_W * WF_SCENE_H);
  for (int d = 0; d < woof_dog_count(); d++) {
    int32_t s[WF_FIELDS];
    woof_default_dog(d, s);
    if (place >= 0) s[WF_PLACE] = place;
    woof_scene(s, step, bark ? 100 : 0, -1, px.data());
    int ox = (d % 4) * w, oy = (d / 4) * h;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        uint32_t c = px[(y / k) * WF_SCENE_W + x / k];
        uint8_t* o = &rgb[(((size_t)(oy + y)) * w * 4 + ox + x) * 3];
        o[0] = (uint8_t)(c >> 16), o[1] = (uint8_t)(c >> 8), o[2] = (uint8_t)c;
      }
  }
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fwrite(rgb.data(), 1, rgb.size(), f);
  fclose(f);
  printf("%d x %d\n", w * 4, h * rows);
}
