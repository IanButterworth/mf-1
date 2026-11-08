// Checks and a sound report for the Almanac months, through the offline engine.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "AlmanacCore.h"
#include "MF1Core.h"

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

void noteOn(int note) {
  const uint8_t m[] = {0x90, (uint8_t)note, 100};
  mf1_inject_midi(m, 3);
}
void noteOff(int note) {
  const uint8_t m[] = {0x80, (uint8_t)note, 0};
  mf1_inject_midi(m, 3);
}

double db(double x) { return 20 * std::log10(std::max(1e-9, x)); }

double rms(const std::vector<float>& a, double from, double to) {
  size_t i0 = (size_t)(from * kRate), i1 = std::min(a.size(), (size_t)(to * kRate));
  double sum = 0;
  for (size_t i = i0; i < i1; i++) sum += (double)a[i] * a[i];
  return std::sqrt(sum / std::max<size_t>(1, i1 - i0));
}

double loudest(const std::vector<float>& a, double upTo) {
  double best = -200;
  for (double t = 0; t + 0.1 <= upTo; t += 0.05) best = std::max(best, db(rms(a, t, t + 0.1)));
  return best;
}

bool finite(const std::vector<float>& a) {
  for (float x : a)
    if (!std::isfinite(x)) return false;
  return true;
}

// Zero crossings per second: a rough brightness, enough to tell a high hush from a low rumble.
double crossings(const std::vector<float>& a, double from, double to) {
  size_t i0 = (size_t)(from * kRate), i1 = std::min(a.size(), (size_t)(to * kRate));
  int n = 0;
  for (size_t i = i0 + 1; i < i1; i++) n += (a[i - 1] < 0) != (a[i] < 0);
  return n / std::max(1e-9, (double)(i1 - i0) / kRate);
}

// Times the level rises above a fraction of the loudest point after being below it: note starts.
int onsets(const std::vector<float>& a, double upTo) {
  std::vector<double> env;
  for (double t = 0; t + 0.01 <= upTo; t += 0.01) env.push_back(rms(a, t, t + 0.01));
  double top = *std::max_element(env.begin(), env.end());
  int n = 0;
  bool high = false;
  for (double e : env) {
    if (!high && e > top * 0.6) n++, high = true;
    if (high && e < top * 0.35) high = false;
  }
  return n;
}

void silence() {
  mf1_all_notes_off();
  for (int i = 0; i < 300; i++)
    if (i >= 3 && rms(render(0.1), 0, 0.1) < 1e-6) break;
}

// Offline, the engine drops commands once its queue is full, so render a little after the 128 bank presets.
void install() {
  almanac_install();
  render(0.05);
}

std::string title(int i) {
  AlmanacMonth m;
  almanac_month(i, &m);
  return m.month;
}

// A month's voice on its own: single notes, clear sky, no weather, no room or echo.
void bare(int month, int32_t s[AL_FIELDS]) {
  almanac_default_day(month, s);
  s[AL_SKY] = AL_CLEAR, s[AL_ELEMENTS] = 0, s[AL_DISTANCE] = 0, s[AL_ECHO] = 0, s[AL_PLAY] = AL_SINGLE;
}

// Plays the notes one key gives, held for `hold` seconds, then released for `tail`.
std::vector<float> play(const int32_t* day, int note, double hold = 1.5, double tail = 1.0, bool left = false) {
  mf1_global_set(MF1_G_QUIRKS, 1);
  almanac_load(day);
  silence();
  render(0.05);
  int notes[8];
  int n = almanac_key_notes(day, note, left, notes);
  for (int i = 0; i < n; i++) mf1_key_down(notes[i]);
  auto a = render(hold);
  for (int i = 0; i < n; i++) mf1_key_up(notes[i]);
  auto b = render(tail);
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// The weather alone: the four operators of music muted.
std::vector<float> weatherOnly(int month, int skyIndex, int elements, int note) {
  int32_t s[AL_FIELDS];
  bare(month, s);
  s[AL_SKY] = skyIndex, s[AL_ELEMENTS] = elements, s[AL_WEATHER] = 0;
  mf1_global_set(MF1_G_QUIRKS, 1);
  almanac_load(s);
  silence();
  mf1_set_op_mutes(0x0f);
  render(0.05);
  noteOn(note);
  auto a = render(2.0);
  noteOff(note);
  mf1_set_op_mutes(0);
  return a;
}

}  // namespace

void almanacReport(int note) {
  install();
  const int n = almanac_month_count();
  printf("note %d, month as composed (bare: clear sky, dry) | loudest dB bare / composed | rms dB at 0.1 0.5 1.4 s, +0.3 s after release | crossings/s\n", note);
  for (int i = 0; i < n; i++) {
    int32_t s[AL_FIELDS];
    bare(i, s);
    auto a = play(s, note);
    almanac_default_day(i, s);
    auto c = play(s, note);
    printf("%-10s %6.1f / %6.1f | %6.1f %6.1f %6.1f %6.1f | %6.0f\n", title(i).c_str(), loudest(a, 1.5), loudest(c, 1.5),
           db(rms(a, 0.08, 0.12)), db(rms(a, 0.45, 0.55)), db(rms(a, 1.35, 1.45)), db(rms(a, 1.75, 1.85)),
           crossings(a, 0.2, 1.2));
  }
  printf("\nthe weather alone (January's voice muted), Elements 50 and 100: rms dB 0.5-1.5 s, crossings/s at notes 48 / 72\n");
  for (int k = 0; k < AL_SKY_COUNT; k++) {
    auto a = weatherOnly(0, k, 50, 48), b = weatherOnly(0, k, 100, 48), c = weatherOnly(0, k, 100, 72);
    printf("%-8s %6.1f %6.1f | %7.0f / %7.0f\n", almanac_sky_name(k), db(rms(a, 0.5, 1.5)), db(rms(b, 0.5, 1.5)),
           crossings(b, 0.5, 1.5), crossings(c, 0.5, 1.5));
  }
}

int almanacTests() {
  failures = 0;
  mf1_global_set(MF1_G_QUIRKS, 1);
  install();
  const int n = almanac_month_count();
  check(n == 12, "there are twelve months");

  {
    bool names = true, ranges = true, voices = true;
    int styles[AL_PLAY_COUNT] = {0};
    for (int i = 0; i < n; i++) {
      AlmanacMonth m;
      names &= almanac_month(i, &m) && strlen(m.name) > 0 && strlen(m.month) > 0 && strlen(m.title) > 0 && strlen(m.blurb) > 0;
      int32_t s[AL_FIELDS], c[AL_FIELDS];
      almanac_default_day(i, s);
      memcpy(c, s, sizeof(s));
      almanac_clamp(c);
      ranges &= memcmp(s, c, sizeof(s)) == 0 && s[AL_MONTH] == i;
      styles[s[AL_PLAY]]++;
      almanac_load(s);
      uint8_t v[155];
      almanac_voice(s, v);
      for (int k = 0; k < 145; k++) voices &= mf1_edit_get(k) == v[k];
    }
    check(names, "every month has a voice name, a month, a title and a few words");
    check(ranges, "every month's day is in range and names its month");
    bool all = true;
    for (int k = 0; k < AL_PLAY_COUNT; k++) all &= styles[k] >= 2;
    check(all, "the months as composed use every playing style, each at least twice");
    check(voices, "a loaded month's edit buffer is exactly its voice");
  }

  {
    uint8_t dump[4104];
    almanac_export_bank(dump);
    int sum = 0;
    for (int i = 6; i < 6 + 4096; i++) sum += dump[i];
    check(dump[0] == 0xF0 && dump[3] == 0x09 && dump[4103] == 0xF7 && ((sum + dump[6 + 4096]) & 0x7f) == 0,
          "the month bank is a valid DX7 32-voice dump");
  }

  // Every month plays, at about the same loudness as the others. The weather is left out: its LFO runs
  // free, so how loud it is at one moment depends on what played before.
  {
    double lo = 1e9, hi = -1e9;
    bool ok = true;
    for (int i = 0; i < n; i++) {
      int32_t s[AL_FIELDS];
      bare(i, s);
      auto a = play(s, 60);
      ok &= finite(a);
      double l = loudest(a, 1.5);
      lo = std::min(lo, l), hi = std::max(hi, l);
    }
    char what[120];
    snprintf(what, sizeof(what), "every month plays, within 9 dB of the others at C4 (%.1f to %.1f dB)", lo, hi);
    check(ok && lo > -40 && hi - lo <= 9, what);
  }

  // The weather: silent under a clear sky or at Elements 0, heard under every other sky, and tuned to
  // the key, so two octaves up it runs four times as fast.
  {
    bool clear = rms(weatherOnly(0, AL_CLEAR, 100, 60), 0.2, 1.5) < 1e-4;
    bool none = rms(weatherOnly(0, AL_RAIN, 0, 60), 0.2, 1.5) < 1e-4;
    bool heard = true, fixed = true;
    for (int k = AL_BREEZE; k < AL_SKY_COUNT; k++) {
      auto lowKey = weatherOnly(0, k, 100, 48), highKey = weatherOnly(0, k, 100, 72);
      heard &= db(rms(lowKey, 0.3, 2.0)) > -50;
      double a = crossings(lowKey, 0.5, 1.5), b = crossings(highKey, 0.5, 1.5);
      fixed &= b > 3 * a && b < 5 * a;
      if (!(b > 3 * a && b < 5 * a) || db(rms(lowKey, 0.3, 2.0)) <= -50)
        printf("     %s: %.1f dB, %.0f / %.0f crossings/s\n", almanac_sky_name(k), db(rms(lowKey, 0.3, 2.0)), a, b);
    }
    check(clear && none, "a clear sky, or Elements at 0, adds no weather");
    check(heard, "breeze, rain, fog, storm and snow are heard under the notes");
    check(fixed, "the weather follows the key it is played on");
    double snow = crossings(weatherOnly(0, AL_SNOW, 100, 60), 0.5, 1.5);
    double fog = crossings(weatherOnly(0, AL_FOG, 100, 60), 0.5, 1.5);
    check(snow > 4 * fog, "snow glints high and fog hums low");
  }

  // Mood: wistful opens a note more slowly than glad.
  {
    int32_t s[AL_FIELDS];
    bare(5, s);
    s[AL_MOOD] = 5;
    auto sad = play(s, 60, 1.0, 0.2);
    s[AL_MOOD] = 95;
    auto glad = play(s, 60, 1.0, 0.2);
    auto rise = [](const std::vector<float>& a) {
      double top = rms(a, 0.8, 0.9);
      for (double t = 0; t < 0.9; t += 0.005)
        if (rms(a, t, t + 0.01) > top * 0.5) return t;
      return 1.0;
    };
    check(rise(sad) > rise(glad) * 1.5, "a wistful mood opens a note more slowly than a glad one");
  }

  // Playing styles.
  {
    int32_t s[AL_FIELDS];
    almanac_default_day(4, s);  // May: a five-note chord
    int notes[8];
    check(almanac_key_notes(s, 60, false, notes) == 1 && notes[0] == 60, "single plays the key");
    s[AL_PLAY] = AL_CHORD;
    int c = almanac_key_notes(s, 60, false, notes);
    check(c == 5 && notes[0] == 60, "chord plays the month's chord on the key");
    s[AL_VOICING] = AL_WIDE;
    check(almanac_key_notes(s, 60, false, notes) == 6 && notes[0] == 48, "wide voicing adds the root an octave down");
    s[AL_PLAY] = AL_SPLIT, s[AL_VOICING] = AL_OPEN;
    int l = almanac_key_notes(s, 55, true, notes);
    bool drone = l == 2 && notes[0] == 43 && notes[1] == 50;
    check(drone && almanac_key_notes(s, 67, false, notes) == 1 && notes[0] == 67,
          "split gives the left hand a drone of root and fifth below, the right hand single notes");

    almanac_default_day(3, s);  // April
    s[AL_PLAY] = AL_PATTERN, s[AL_TEMPO] = 120, s[AL_STEP] = 1, s[AL_DISTANCE] = 0, s[AL_ECHO] = 0;
    s[AL_ELEMENTS] = 0;
    auto a = play(s, 60, 2.0, 0.1);
    check(mf1_arp_on(), "a pattern switches the arpeggiator on");
    int hits = onsets(a, 2.0);
    char what[80];
    snprintf(what, sizeof(what), "a held key plays a pattern of eighth notes at 120 BPM (%d notes in 2 s)", hits);
    check(hits >= 6 && hits <= 9, what);
    s[AL_PLAY] = AL_SINGLE;
    almanac_apply(s);
    check(!mf1_arp_on(), "single notes switch it off again");
  }

  silence();
  return failures;
}

void almanacWeatherTrace(int skyIndex, int elements) {
  install();
  auto a = weatherOnly(0, skyIndex, elements, 60);
  for (double t = 0; t < 2.0; t += 0.1) printf("%4.1f %6.1f %6.0f\n", t, db(rms(a, t, t + 0.1)), crossings(a, t, t + 0.1));
}


