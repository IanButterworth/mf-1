// Checks for the Piano mode's engine, through the offline engine with the piano installed as its instrument.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "MF1Core.h"
#include "PianoCore.h"

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

double db(double x) { return 20 * std::log10(std::max(1e-12, x)); }

double rms(const std::vector<float>& a, double from, double to) {
  size_t i0 = (size_t)(std::max(0.0, from) * kRate), i1 = std::min(a.size(), (size_t)(to * kRate));
  double sum = 0;
  for (size_t i = i0; i < i1; i++) sum += (double)a[i] * a[i];
  return std::sqrt(sum / std::max<size_t>(1, i1 - std::min(i0, i1)));
}

// The strongest frequency between lo and hi Hz over a stretch of the sound, by a fine scan of single DFT bins.
double peakFreq(const std::vector<float>& a, double from, double len, double lo, double hi) {
  size_t i0 = (size_t)(from * kRate), n = (size_t)(len * kRate);
  double best = 0, bestF = lo;
  for (double f = lo; f <= hi; f += 0.05) {
    double re = 0, im = 0, w = 2 * M_PI * f / kRate;
    for (size_t i = 0; i < n; i++) {
      double h = 0.5 - 0.5 * std::cos(2 * M_PI * i / n);
      re += a[i0 + i] * h * std::cos(w * i);
      im += a[i0 + i] * h * std::sin(w * i);
    }
    if (re * re + im * im > best) {
      best = re * re + im * im;
      bestF = f;
    }
  }
  return bestF;
}

// Spectral centroid of a stretch, in Hz, from a plain DFT on a coarse grid.
double centroid(const std::vector<float>& a, double from, double len) {
  size_t i0 = (size_t)(from * kRate), n = (size_t)(len * kRate);
  double num = 0, den = 0;
  for (double f = 50; f < 8000; f += 25) {
    double re = 0, im = 0, w = 2 * M_PI * f / kRate;
    for (size_t i = 0; i < n; i++) {
      re += a[i0 + i] * std::cos(w * i);
      im += a[i0 + i] * std::sin(w * i);
    }
    num += f * (re * re + im * im);
    den += re * re + im * im;
  }
  return num / std::max(1e-20, den);
}

// The share of a stretch's energy below hz.
double lowShare(const std::vector<float>& a, double from, double len, double hz) {
  size_t i0 = (size_t)(from * kRate), n = (size_t)(len * kRate);
  double lo = 0, all = 0;
  for (double f = 50; f < 8000; f += 25) {
    double re = 0, im = 0, w = 2 * M_PI * f / kRate;
    for (size_t i = 0; i < n; i++) {
      re += a[i0 + i] * std::cos(w * i);
      im += a[i0 + i] * std::sin(w * i);
    }
    all += re * re + im * im;
    if (f < hz) lo += re * re + im * im;
  }
  return lo / std::max(1e-20, all);
}

std::vector<float> strike(int note, int vel, double hold, double total, bool pedal = false) {
  mf1_all_notes_off();
  render(1.5);
  if (pedal) mf1_sustain(true);
  mf1_key_down_velocity(note, vel);
  std::vector<float> a = render(hold);
  mf1_key_up(note);
  std::vector<float> b = render(total - hold);
  a.insert(a.end(), b.begin(), b.end());
  if (pedal) mf1_sustain(false);
  return a;
}

}  // namespace

int pianoTests() {
  failures = 0;
  mf1_offline_init(kRate);
  mf1_global_set(MF1_G_QUIRKS, 0);
  mf1_global_set(MF1_G_OCTAVE, 0);
  mf1_global_set(MF1_G_TRANSPOSE, 0);
  mf1_arp_enable(false);
  mf1_seq_enable(false);
  piano_install();
  int32_t s[PN_FIELDS];
  piano_default(0, s);
  s[PN_F_ROOM] = 0;  // the reverb would blur the decays these checks measure
  piano_apply(s);
  render(0.1);

  std::vector<float> c4 = strike(60, 110, 4.0, 4.5);
  float peak = 0;
  bool fine = true;
  for (float x : c4) {
    peak = std::max(peak, std::fabs(x));
    fine &= std::isfinite(x);
  }
  check(fine && peak > 0.05f && peak < 0.9f, "piano: C4 at ff sounds, without clipping (peak " + std::to_string(peak) + ")");

  // The strings are stiff, so the tenth partial lies sharp of ten times the first.
  double f1 = peakFreq(c4, 0.3, 1.0, 255, 270), f10 = peakFreq(c4, 0.05, 0.5, 2560, 2720);
  double cents = 1200 * std::log2(f10 / (10 * f1));
  check(cents > 8 && cents < 40,
        "piano: C4's tenth partial is " + std::to_string(cents).substr(0, 5) + " cents sharp, as a stiff string's is");

  // Two stages: a fast fall after the strike, then a long slow one.
  double early = (db(rms(c4, 0.05, 0.15)) - db(rms(c4, 0.75, 0.85))) / 0.7;
  double late = (db(rms(c4, 2.0, 2.1)) - db(rms(c4, 3.6, 3.7))) / 1.6;
  check(early > 1.5 * late && late > 0.5,
        "piano: C4 falls " + std::to_string((int)early) + " dB/s at first, then " + std::to_string((int)late) + " dB/s");

  // Velocity: softer is quieter and darker.
  std::vector<float> soft = strike(60, 30, 1.0, 1.0);
  double dl = db(rms(c4, 0.0, 0.3)) - db(rms(soft, 0.0, 0.3));
  double cl = centroid(c4, 0.02, 0.1), cs = centroid(soft, 0.02, 0.1);
  check(dl > 12 && cs < cl, "piano: velocity 30 is " + std::to_string((int)dl) + " dB quieter than 110, centroid " +
                                std::to_string((int)cs) + " Hz against " + std::to_string((int)cl));

  // The damper stops a released note within half a second; the pedal keeps it.
  double before = db(rms(c4, 3.9, 4.0)), after = db(rms(c4, 4.4, 4.5));
  check(before - after > 25, "piano: the damper takes C4 down " + std::to_string((int)(before - after)) + " dB in 0.4 s");
  std::vector<float> ped = strike(60, 110, 1.0, 2.0, true);
  std::vector<float> dry = strike(60, 110, 1.0, 2.0, false);
  double kept = db(rms(ped, 1.8, 1.9)) - db(rms(dry, 1.8, 1.9));
  check(kept > 20, "piano: with the pedal down a released note rings on, " + std::to_string((int)kept) + " dB above without");

  // Above the dampers a note rings on after its key comes up.
  std::vector<float> top = strike(96, 110, 0.2, 1.0);
  check(db(rms(top, 0.6, 0.7)) - db(rms(top, 0.05, 0.15)) > -30, "piano: C7 has no damper and rings on after its key");

  // The knock: in the treble, the strike carries sound far below the note, which the string alone does not.
  double knock = lowShare(top, 0.0, 0.03, 1000), tone = lowShare(top, 0.4, 0.03, 1000);
  check(knock > 0.02 && knock > 10 * tone, "piano: C7's strike carries the knock: " + std::to_string((int)(knock * 1000) / 10.0).substr(0, 4) +
                                "% of its energy below 1 kHz, against " + std::to_string(tone * 100).substr(0, 4) + "% later");

  // A loud pedalled cluster fills the pool to its budget and no further, and costs little.
  mf1_all_notes_off();
  render(1.5);
  mf1_sustain(true);
  auto t0 = std::chrono::steady_clock::now();
  for (int n : {24, 31, 36, 43, 48, 55, 60, 64, 67, 72, 76, 84}) mf1_key_down_velocity(n, 120);
  std::vector<float> big = render(2.0);
  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  int used = piano_oscillators();
  check(used <= piano_budget() && mf1_active_voices() <= piano_budget(),
        "piano: twelve pedalled notes hold " + std::to_string(used) + " oscillators of " + std::to_string(piano_budget()));
  printf("     12 pedalled notes: 2 s of audio in %.3f s (%.2f%% of one core)\n", secs, 100 * secs / 2.0);
  float bp = 0;
  for (float x : big) bp = std::max(bp, std::fabs(x));
  check(bp < 1.0f, "piano: twelve notes at velocity 120 stay below full scale (peak " + std::to_string(bp) + ")");
  for (int n : {24, 31, 36, 43, 48, 55, 60, 64, 67, 72, 76, 84}) mf1_key_up(n);
  mf1_sustain(false);
  render(1.0);

  // Taking the piano away gives the DX7 voices back.
  mf1_set_instrument(nullptr);
  mf1_load_preset(0);
  mf1_key_down(60);
  std::vector<float> dx = render(0.3);
  mf1_key_up(60);
  render(1.0);
  check(rms(dx, 0.05, 0.3) > 1e-3, "piano: without the instrument the DX7 voices play again");
  return failures;
}
