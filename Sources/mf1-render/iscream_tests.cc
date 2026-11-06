// Checks and a sound report for the iScream flavors, through the offline engine.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "IScreamCore.h"
#include "MF1Core.h"

namespace {

const double kRate = 48000;
int failures = 0;

void check(bool ok, const std::string& what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) failures++;
}

std::vector<float> render(int frames) {
  std::vector<float> out(frames);
  for (int i = 0; i < frames; i += 64) mf1_offline_render(out.data() + i, std::min(64, frames - i));
  return out;
}

void noteOn(int note, int vel = 100) {
  const uint8_t m[] = {0x90, (uint8_t)note, (uint8_t)vel};
  mf1_inject_midi(m, 3);
}
void noteOff(int note) {
  const uint8_t m[] = {0x80, (uint8_t)note, 0};
  mf1_inject_midi(m, 3);
}

void silence() {
  mf1_all_notes_off();
  // Let the last take die away completely, so it can't colour the next.
  for (int i = 0; i < 80; i++) {
    auto a = render((int)(kRate * 0.1));
    double sum = 0;
    for (float v : a) sum += (double)v * v;
    if (i >= 3 && std::sqrt(sum / a.size()) < 1e-6) break;
  }
}

double db(double x) { return 20 * std::log10(std::max(1e-9, x)); }

double rms(const std::vector<float>& a, double from, double to) {
  size_t i0 = (size_t)(from * kRate), i1 = std::min(a.size(), (size_t)(to * kRate));
  double sum = 0;
  for (size_t i = i0; i < i1; i++) sum += (double)a[i] * a[i];
  return std::sqrt(sum / std::max<size_t>(1, i1 - i0));
}

// The loudest 100 ms of a take, in dB: how loud a flavor plays, whether it swells or strikes.
double loudest(const std::vector<float>& a, double upTo = 1.5) {
  double best = -200;
  for (double t = 0; t + 0.1 <= upTo; t += 0.05) best = std::max(best, db(rms(a, t, t + 0.1)));
  return best;
}

float peak(const std::vector<float>& a) {
  float p = 0;
  for (float x : a) p = std::max(p, std::fabs(x));
  return p;
}

bool finite(const std::vector<float>& a) {
  for (float x : a)
    if (!std::isfinite(x)) return false;
  return true;
}

// Magnitude spectrum of a Hann windowed segment, with the bin spacing.
struct Spectrum {
  std::vector<double> mag;
  double binHz;
};

Spectrum spectrum(const std::vector<float>& a, double from, int n = 8192) {
  std::vector<std::complex<double>> x(n);
  size_t i0 = (size_t)(from * kRate);
  for (int i = 0; i < n; i++) {
    double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1));
    x[i] = i0 + i < a.size() ? a[i0 + i] * w : 0;
  }
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(x[i], x[j]);
  }
  for (int len = 2; len <= n; len <<= 1) {
    std::complex<double> wl = std::polar(1.0, -2 * M_PI / len);
    for (int i = 0; i < n; i += len) {
      std::complex<double> w = 1;
      for (int k = 0; k < len / 2; k++) {
        auto u = x[i + k], v = x[i + k + len / 2] * w;
        x[i + k] = u + v;
        x[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
  Spectrum s;
  s.mag.resize(n / 2);
  for (int i = 0; i < n / 2; i++) s.mag[i] = std::abs(x[i]) / n;
  s.binHz = kRate / n;
  return s;
}

// Brightness: the spectral centroid between 100 Hz and 10 kHz.
double centroid(const std::vector<float>& a, double from) {
  Spectrum s = spectrum(a, from);
  double num = 0, den = 0;
  for (size_t i = (size_t)(100 / s.binHz); i < (size_t)(10000 / s.binHz) && i < s.mag.size(); i++) {
    double p = s.mag[i] * s.mag[i];
    num += p * i * s.binHz;
    den += p;
  }
  return den > 0 ? num / den : 0;
}

// Level of harmonic n of f0, in dB below the strongest harmonic.
std::vector<double> harmonics(const std::vector<float>& a, double from, double f0, int count) {
  Spectrum s = spectrum(a, from);
  std::vector<double> h;
  double top = 1e-12;
  for (int n = 1; n <= count; n++) {
    double best = 0;
    int c = (int)std::lround(n * f0 / s.binHz);
    for (int i = std::max(1, c - 3); i <= c + 3 && i < (int)s.mag.size(); i++) best = std::max(best, s.mag[i]);
    h.push_back(best);
    top = std::max(top, best);
  }
  for (double& v : h) v = db(v / top);
  return h;
}

// Share of the energy above 800 Hz, three octaves up from a low C and well above the fundamental:
// a measure of brightness that works for plucks, bells and the like.
double bright(const std::vector<float>& a, double from, int n = 4096) {
  Spectrum s = spectrum(a, from, n);
  double hi = 0, all = 1e-18;
  for (size_t i = 1; i < s.mag.size(); i++) {
    double p = s.mag[i] * s.mag[i];
    all += p;
    if (i * s.binHz > 800) hi += p;
  }
  return hi / all;
}

double hz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// One note through the whole sundae: the render is `hold` seconds held, then `tail` after release.
struct Take {
  std::vector<float> held, tail;
  std::vector<float> all() const {
    auto a = held;
    a.insert(a.end(), tail.begin(), tail.end());
    return a;
  }
};

Take play(const int32_t* sundae, int note, double hold = 2.0, double tail = 2.0) {
  mf1_global_set(MF1_G_QUIRKS, 1);
  silence();
  iscream_load(sundae);
  render(2048);
  Take t;
  noteOn(note);
  t.held = render((int)(kRate * hold));
  noteOff(note);
  t.tail = render((int)(kRate * tail));
  return t;
}

// The flavor on its own: no cone, no toppings.
void bare(int flavor, int32_t s[IS_FIELDS]) {
  iscream_default_sundae(flavor, s);
  s[IS_CONE_ON] = 0;
  for (int t = 0; t < IS_TOPPING_COUNT; t++) s[IS_TOP(t, 0)] = 0;
}

std::string flavorTitle(int i) {
  IScreamFlavor f;
  iscream_flavor(i, &f);
  return f.title;
}

}  // namespace

// A printed look at each flavor: level, how it moves and how bright it is.
void iscreamReport(int only, int note, int scoops, bool withToppings) {
  mf1_global_set(MF1_G_QUIRKS, 1);
  {
    // The factory voices, for how loud a voice usually is here.
    std::vector<double> r, p;
    for (int i = 0; i < 128; i++) {
      silence();
      mf1_load_preset(i);
      render(2048);
      noteOn(note);
      auto a = render((int)(kRate * 0.5));
      noteOff(note);
      r.push_back(db(rms(a, 0.05, 0.45)));
      p.push_back(db(peak(a)));
    }
    std::sort(r.begin(), r.end());
    std::sort(p.begin(), p.end());
    printf("factory voices at note %d: rms dB p10 %.1f median %.1f p90 %.1f | peak dB p10 %.1f median %.1f p90 %.1f\n", note, r[12],
           r[64], r[115], p[12], p[64], p[115]);
  }
  iscream_install();
  const int n = iscream_flavor_count();
  {
    // A lone sine at output level 99 is the yardstick for how loud a flavor is.
    silence();
    mf1_load_preset(n + 3);
    render(2048);
    noteOn(note);
    auto a = render((int)(kRate * 0.5));
    printf("a single sine at OL 99: peak %.3f, rms %.1f dB\n", peak(a), db(rms(a, 0.1, 0.4)));
    noteOff(note);
    render((int)(kRate * 0.5));
  }
  printf("note %d (%.1f Hz), %d scoop(s), %s\n", note, hz(note), scoops, withToppings ? "default sundae" : "voice only");
  printf("%-15s %6s %7s | level dB at 0.05 0.2 0.5 1.0 1.9 s | release -dB at +0.2 +0.5 +1.5 | centroid Hz 0.1 s / 1 s | harmonics dB (H1..H8)\n",
         "flavor", "peak", "rms dB");
  for (int i = 0; i < n; i++) {
    if (only >= 0 && i != only) continue;
    int32_t s[IS_FIELDS];
    if (withToppings)
      iscream_default_sundae(i, s);
    else
      bare(i, s);
    s[IS_SCOOPS] = scoops;
    Take t = play(s, note);
    auto a = t.all();
    double ref = rms(a, 0.1, 0.4);
    auto lvl = [&](double from, double to) { return db(rms(a, from, to) / std::max(1e-9, ref)); };
    printf("%-15s %6.3f %7.1f | %5.1f %5.1f %5.1f %5.1f %5.1f | %6.1f %6.1f %6.1f | %6.0f / %6.0f |", flavorTitle(i).c_str(),
           peak(a), db(ref), lvl(0.03, 0.08), lvl(0.15, 0.25), lvl(0.45, 0.55), lvl(0.95, 1.05), lvl(1.85, 1.95),
           lvl(2.15, 2.25), lvl(2.45, 2.55), lvl(3.45, 3.55), centroid(a, 0.1), centroid(a, 1.0));
    for (double h : harmonics(a, 0.1, hz(note), 8)) printf(" %5.1f", h);
    printf("\n");
  }
}

// The level of one note in 100 ms steps (held 1 s, then released), for each of three settings of a field.
void iscreamEnvelope(int flavor, int field, int a, int b, int c) {
  mf1_global_set(MF1_G_QUIRKS, 1);
  iscream_install();
  printf("%s, field %d at %d / %d / %d: level dB per 100 ms, note off at 1.0 s\n", flavorTitle(flavor).c_str(), field, a, b, c);
  std::vector<std::vector<float>> takes;
  for (int v : {a, b, c}) {
    int32_t s[IS_FIELDS];
    bare(flavor, s);
    s[field] = v;
    takes.push_back(play(s, 60, 0.6, 3.0).all());
  }
  for (double t = 0; t < 4.0; t += 0.1) {
    printf("%4.1f s", t);
    for (auto& tk : takes) printf("  %7.1f", db(rms(tk, t, t + 0.1)));
    printf("\n");
  }
}

int iscreamTests() {
  failures = 0;
  mf1_global_set(MF1_G_QUIRKS, 1);
  iscream_install();
  const int n = iscream_flavor_count();

  check(n >= 12 && n <= 32, "there are 12 to 32 flavors");

  // Names, defaults and ranges.
  {
    bool names = true, ranges = true, unique = true;
    for (int i = 0; i < n; i++) {
      IScreamFlavor f;
      names &= iscream_flavor(i, &f) && strlen(f.name) > 0 && strlen(f.title) > 0 && strlen(f.blurb) > 0;
      for (int j = 0; j < i; j++) {
        IScreamFlavor g;
        iscream_flavor(j, &g);
        unique &= strcmp(f.title, g.title) != 0 && strcmp(f.name, g.name) != 0;
      }
      int32_t s[IS_FIELDS], c[IS_FIELDS];
      iscream_default_sundae(i, s);
      memcpy(c, s, sizeof(s));
      iscream_clamp(c);
      ranges &= memcmp(s, c, sizeof(s)) == 0 && s[IS_FLAVOR] == i;
    }
    check(names, "every flavor has a voice name, a title and a few words");
    check(unique, "flavor names are all different");
    check(ranges, "default sundaes are in range and name their flavor");
  }

  // The bank dump is a valid DX7 bank, and the engine holds the flavors as its presets.
  {
    uint8_t dump[4104];
    iscream_export_bank(dump);
    int sum = 0;
    for (int i = 6; i < 6 + 4096; i++) sum += dump[i];
    check(dump[0] == 0xF0 && dump[1] == 0x43 && dump[3] == 0x09 && dump[4] == 0x20 && dump[5] == 0x00 && dump[4103] == 0xF7,
          "the bank dump has the DX7 32-voice header and ends with F7");
    check(((sum + dump[6 + 4096]) & 0x7f) == 0, "the bank dump checksum is right");
    bool names = true, voices = true;
    for (int i = 0; i < n; i++) {
      IScreamFlavor f;
      iscream_flavor(i, &f);
      char name[11];
      mf1_preset_name(i, name);
      std::string a(name), b(f.name);
      while (!a.empty() && a.back() == ' ') a.pop_back();
      names &= a == b;
      mf1_load_preset(i);
      int32_t s[IS_FIELDS];
      iscream_default_sundae(i, s);
      s[IS_SCOOPS] = 1, s[IS_SWEET] = 50, s[IS_MELT] = 50, s[IS_FIZZ] = 0, s[IS_TOP(IS_CARAMEL, 0)] = 0;
      uint8_t v[155];
      iscream_voice(s, v);
      for (int k = 0; k < 145; k++) voices &= mf1_edit_get(k) == v[k];
    }
    check(names, "the engine's presets carry the flavor names");
    check(voices, "a loaded flavor is exactly its designed voice");
    mf1_load_preset(n + 3);
    char name[11];
    mf1_preset_name(n + 3, name);
    check(strncmp(name, "INIT SCOOP", 10) == 0, "slots after the flavors are empty voices");
    check(mf1_preset_count() == 128, "iScream stays within the FM-1's 128 presets");
  }

  // Every flavor, at one, two and three scoops, sounds, stays finite and leaves headroom.
  {
    bool sounds = true, finiteAll = true, headroom = true, tails = true;
    double lo = 1e9, hi = -1e9;
    std::string loName, hiName;
    for (int i = 0; i < n; i++) {
      for (int scoops = 1; scoops <= 3; scoops++) {
        int32_t s[IS_FIELDS];
        iscream_default_sundae(i, s);
        s[IS_SCOOPS] = scoops;
        Take t = play(s, 60, 1.2, 3.0);
        auto a = t.all();
        finiteAll &= finite(a);
        double r = loudest(a);
        sounds &= r > -45;
        headroom &= peak(a) < 0.98f;
        if (scoops == 1) {
          if (r < lo) lo = r, loName = flavorTitle(i);
          if (r > hi) hi = r, hiName = flavorTitle(i);
        }
      }
    }
    check(finiteAll, "all flavors render finite samples at every scoop count");
    check(sounds, "all flavors sound at every scoop count");
    check(headroom, "no default sundae reaches full scale on one note");
    printf("     loudest 100 ms at C4: %.1f dB (%s) to %.1f dB (%s)\n", lo, loName.c_str(), hi, hiName.c_str());
    check(hi - lo < 9.0, "flavors are within 9 dB of each other in loudness");
    for (int i = 0; i < n; i++) {
      int32_t s[IS_FIELDS];
      bare(i, s);
      double tail = db(rms(play(s, 60, 1.0, 4.0).all(), 4.0, 4.2));
      if (tail > -60) printf("     %s: %.1f dB 3 s after release\n", flavorTitle(i).c_str(), tail);
      tails &= tail < -60;
    }
    check(tails, "every flavor falls silent within 3 s of its release");
  }

  // Scoops widen the voice, and keep the loudness about the same.
  {
    bool detune = true, level = true;
    for (int i = 0; i < n; i++) {
      int32_t s[IS_FIELDS];
      iscream_default_sundae(i, s);
      uint8_t v1[155], v2[155], v3[155];
      s[IS_SCOOPS] = 1, iscream_voice(s, v1);
      s[IS_SCOOPS] = 2, iscream_voice(s, v2);
      s[IS_SCOOPS] = 3, iscream_voice(s, v3);
      auto spread = [](const uint8_t* v) {
        int lo = 99, hi = 0;
        for (int op = 0; op < 6; op += 2) {
          lo = std::min<int>(lo, v[op * 21 + 20]);
          hi = std::max<int>(hi, v[op * 21 + 20]);
        }
        return hi - lo;
      };
      detune &= spread(v1) < spread(v2) && spread(v2) < spread(v3);
      s[IS_SCOOPS] = 1;
      double a = loudest(play(s, 60, 1.2, 0.1).held);
      s[IS_SCOOPS] = 3;
      double b = loudest(play(s, 60, 1.2, 0.1).held);
      if (std::fabs(a - b) >= 4.5) printf("     %s: %.1f dB at one scoop, %.1f dB at three\n", flavorTitle(i).c_str(), a, b);
      level &= std::fabs(a - b) < 4.5;
    }
    check(detune, "more scoops detune the stacks further apart");
    check(level, "one and three scoops differ by less than 4.5 dB in loudness");
  }

  // Sweet brightens, Melt lengthens, the cone filters and each topping changes the sound.
  {
    bool sweet = true, melt = true, cone = true;
    for (int i = 0; i < n; i++) {
      int32_t s[IS_FIELDS];
      bare(i, s);
      s[IS_SWEET] = 15;
      double dark = bright(play(s, 60, 1.2, 0.1).held, 0.0, 32768);
      s[IS_SWEET] = 85;
      double light = bright(play(s, 60, 1.2, 0.1).held, 0.0, 32768);
      if (!(light > dark * 1.1)) {
        printf("     %s: Sweet 15 puts %.2f%% above 800 Hz, Sweet 85 %.2f%%\n", flavorTitle(i).c_str(), dark * 100, light * 100);
        sweet = false;
      }
      s[IS_SWEET] = 50;
      s[IS_MELT] = 15;
      auto shortTake = play(s, 60, 0.6, 3.0).tail;
      s[IS_MELT] = 90;
      auto longTake = play(s, 60, 0.6, 3.0).tail;
      double es = rms(shortTake, 0, 3), el = rms(longTake, 0, 3);
      if (!(el > es * 1.3)) {
        printf("     %s: Melt 15 leaves %.1f dB after release, Melt 90 %.1f dB\n", flavorTitle(i).c_str(), db(es), db(el));
        melt = false;
      }
      s[IS_MELT] = 50;
      s[IS_CONE_ON] = 1, s[IS_CONE_STYLE] = 0, s[IS_CRUNCH] = 0;
      s[IS_TOAST] = 0;
      double closed = rms(play(s, 60, 1.0, 0.2).held, 0.3, 0.9);
      s[IS_TOAST] = 107;
      double open = rms(play(s, 60, 1.0, 0.2).held, 0.3, 0.9);
      if (!(open > closed * 1.15)) {
        printf("     %s: Toast 0 leaves %.1f dB, Toast 107 %.1f dB\n", flavorTitle(i).c_str(), db(closed), db(open));
        cone = false;
      }
    }
    check(sweet, "Sweet makes every flavor brighter");
    check(melt, "Melt makes every flavor hang on longer");
    check(cone, "opening the cone makes every flavor brighter");
  }

  // Toppings: each one changes the sound on its own, in the way it is named for.
  {
    int32_t s[IS_FIELDS];
    bare(8, s);  // Butter Pecan: a pluck that is silent soon after the note
    auto base = play(s, 60, 0.8, 3.0).all();
    auto diff = [&](const std::vector<float>& a) {
      double d = 0, e = 0;
      for (size_t i = 0; i < std::min(a.size(), base.size()); i++) d += (a[i] - base[i]) * (a[i] - base[i]), e += base[i] * base[i];
      return std::sqrt(d / std::max(1e-12, e));
    };
    const int knobs[IS_TOPPING_COUNT][3] = {{80, 40, 80}, {70, 50, 60}, {80, 50, 70}, {70, 60, 50}, {70, 70, 1}};
    const char* names[] = {"Whipped Cream", "Hot Fudge", "Caramel", "Chocolate Chips", "Rainbow Sprinkles"};
    for (int t = 0; t < IS_TOPPING_COUNT; t++) {
      int32_t w[IS_FIELDS];
      memcpy(w, s, sizeof(w));
      w[IS_TOP(t, 0)] = 1;
      for (int k = 0; k < 3; k++) w[IS_TOP(t, k + 1)] = knobs[t][k];
      auto a = play(w, 60, 0.8, 3.0).all();
      check(diff(a) > 0.05, std::string(names[t]) + " changes the sound");
      if (t == IS_CHOC_CHIPS || t == IS_SPRINKLES) {
        check(rms(a, 2.0, 3.0) > 3 * rms(base, 2.0, 3.0) + 1e-6, std::string(names[t]) + " leaves a tail after the note");
      }
      if (t == IS_HOT_FUDGE) check(centroid(a, 0.3) > centroid(base, 0.3) * 0.5, "Hot Fudge keeps the note's pitch");
    }
    // Caramel's detune and flutter are in the voice itself.
    int32_t c[IS_FIELDS];
    memcpy(c, s, sizeof(c));
    uint8_t v0[155], v1[155];
    iscream_voice(c, v0);
    c[IS_TOP(IS_CARAMEL, 0)] = 1, c[IS_TOP(IS_CARAMEL, 1)] = 90;
    iscream_voice(c, v1);
    check(v1[20] < v0[20] && v1[139] > v0[139], "Caramel tunes the voice a little flat and adds flutter");
  }

  // The chain order: cone, fudge, caramel, cream, chips, sprinkles, after every load.
  {
    int32_t s[IS_FIELDS];
    iscream_default_sundae(0, s);
    mf1_fx_swap(0, 5);
    mf1_fx_swap(1, 3);
    iscream_load(s);
    const int want[6] = {0, 3, 5, 4, 2, 1};
    bool order = true;
    for (int i = 0; i < 6; i++) order &= mf1_fx_order(i) == want[i];
    check(order, "the effects are chained cone, fudge, caramel, cream, chips, sprinkles");
  }

  // Mono flavors are mono, the rest poly, and the envelope overlay stays out of the way.
  {
    bool mono = true, env = true;
    for (int i = 0; i < n; i++) {
      IScreamFlavor f;
      iscream_flavor(i, &f);
      int32_t s[IS_FIELDS];
      iscream_default_sundae(i, s);
      iscream_load(s);
      mono &= mf1_edit_mono() == f.mono;
      env &= mf1_env_get(0) == 0;
    }
    check(mono, "bass flavors are mono and the others poly");
    check(env, "the ENV overlay is off for every flavor");
  }

  // A chord at three scoops with the default sundae does not clip.
  {
    bool clean = true;
    for (int i = 0; i < n; i++) {
      int32_t s[IS_FIELDS];
      iscream_default_sundae(i, s);
      s[IS_SCOOPS] = 3;
      mf1_global_set(MF1_G_QUIRKS, 1);
      silence();
      iscream_load(s);
      render(2048);
      for (int note : {48, 55, 60, 64, 67}) noteOn(note);
      auto a = render((int)(kRate * 0.8));
      for (int note : {48, 55, 60, 64, 67}) noteOff(note);
      render((int)(kRate * 2));
      if (peak(a) >= 1.0f) {
        printf("     %s: chord peak %.2f\n", flavorTitle(i).c_str(), peak(a));
        clean = false;
      }
    }
    check(clean, "a five note chord at three scoops stays below full scale");
  }

  silence();
  return failures;
}
