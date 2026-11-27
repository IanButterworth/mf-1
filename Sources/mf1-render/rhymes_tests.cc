// Checks for Rhymes: the songs, the game and its lights, the notes it plays by itself, the sounds through the offline
// engine, and the pictures.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "MF1Core.h"
#include "RhymesCore.h"

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

double rms(const std::vector<float>& a, double from, double to) {
  size_t i0 = (size_t)(std::max(0.0, from) * kRate), i1 = std::min(a.size(), (size_t)(to * kRate));
  double sum = 0;
  for (size_t i = i0; i < i1; i++) sum += (double)a[i] * a[i];
  return std::sqrt(sum / std::max<size_t>(1, i1 - std::min(i0, i1)));
}

double peak(const std::vector<float>& a) {
  double p = 0;
  for (float x : a) p = std::max(p, (double)std::fabs(x));
  return p;
}

void row(int song, int level, int32_t out[RH_FIELDS]) {
  rh_default(song, out);
  out[RH_LEVEL] = level;
}

// Plays the whole song right, a key each 300 ms, and returns the game.
rh_game_t playThrough(int song, int level, uint32_t t0, int wrongEvery) {
  int32_t r[RH_FIELDS];
  row(song, level, r);
  rh_game_t g;
  rh_game_init(&g, r, t0);
  uint32_t t = t0;
  for (int i = 0; i < g.tune.count; i++) {
    t += 300;
    int note = 0, key = rh_key_of(g.tune.note[i]);
    if (wrongEvery && i % wrongEvery == 0) rh_game_key(&g, key == 0 ? 1 : 0, t, &note);
    rh_game_key(&g, key, t, &note);
  }
  return g;
}

void songs() {
  std::set<std::string> titles;
  for (int s = 0; s < rh_song_count(); s++) {
    rh_tune_t t;
    std::string name = rh_song_title(s);
    int n = rh_tune(s, &t);
    titles.insert(name);
    bool keys = n > 0, words = true, lines = true;
    int lo = 127, hi = 0;
    for (int i = 0; i < n; i++) {
      keys = keys && rh_key_of(t.note[i]) >= 0 && rh_key_of(t.note[i]) < 16 && t.len[i] > 0;
      lo = std::min(lo, (int)t.note[i]);
      hi = std::max(hi, (int)t.note[i]);
      char syl[16];
      rh_syllable(&t, i, syl);
      words = words && syl[0] && syl[0] != ' ';
      lines = lines && (i == 0 || t.line[i] == t.line[i - 1] || t.line[i] == t.line[i - 1] + 1);
    }
    check(n > 0 && n <= RH_MAX_NOTES, name + ": its notes, words and chords agree (" + std::to_string(n) + " notes, " +
                                          std::to_string(t.chords) + " chords)");
    check(keys, name + ": every note is on a white key, at octave 0");
    check(words && lines && t.lines >= 2 && t.line[n - 1] == t.lines - 1, name + ": a syllable for every note, in order of lines");
    check(lo >= 55 && hi <= 76, name + ": stays between G3 and E5, in the middle of the keys");
  }
  check((int)titles.size() == rh_song_count(), "every song has a title of its own");
  rh_tune_t t;
  rh_tune(0, &t);
  char a[16], b[16];
  rh_syllable(&t, 10, a);
  rh_syllable(&t, 11, b);
  check(t.count == 17 && std::string(a) == "two" && std::string(b) == "a", "Hot Cross Buns has 17 notes, its words in place");
  rh_tune(3, &t);
  rh_syllable(&t, 0, a);
  rh_syllable(&t, 1, b);
  check(t.count == 42 && std::string(a) == "Twin-" && std::string(b) == "kle,", "Twinkle's syllables keep their hyphen");
  check(rh_key_of(60) == 4 && rh_note_of(4) == 60 && rh_key_of(61) == 19 && rh_note_of(19) == 61,
        "middle C is the fifth lower key, C# the upper key above it");
}

void game() {
  rh_game_t g = playThrough(0, RH_GUIDE, 1000, 0);
  check(g.state == RH_DONE && g.stars == 3 && g.misses == 0 && g.best_streak == g.tune.count,
        "Guide: every right key finishes the song with three stars");
  g = playThrough(0, RH_GUIDE, 1000, 3);
  check(g.state == RH_DONE && g.misses == 6 && g.stars == 1, "Guide: a wrong key each third note costs stars");

  int32_t r[RH_FIELDS];
  row(2, RH_GUIDE, r);
  rh_game_init(&g, r, 0);
  int note = 0;
  int res = rh_game_key(&g, 0, 100, &note);
  check(res == RH_WRONG && g.pos == 0 && note == -1, "Guide: a wrong key is silent and the song waits");
  rh_game_key(&g, 0, 200, &note);
  check(g.misses == 1, "Guide: two wrong keys on one note count as one miss");
  res = rh_game_key(&g, rh_key_of(g.tune.note[0]), 300, &note);
  check(res == RH_RIGHT && g.pos == 1 && note == g.tune.note[0], "Guide: the right key plays the note and moves on");

  row(2, RH_MAGIC, r);
  rh_game_init(&g, r, 0);
  res = rh_game_key(&g, 26, 100, &note);
  check(res == RH_RIGHT && note == g.tune.note[0] && g.pos == 1, "Magic: any key plays the next note");
  g = playThrough(2, RH_MAGIC, 0, 0);
  check(g.state == RH_DONE && g.stars == 1, "Magic: a finished song earns a star");

  // The lights.
  row(2, RH_GUIDE, r);
  rh_game_init(&g, r, 0);
  uint32_t bright, dim;
  int k0 = rh_key_of(g.tune.note[0]);
  int lit = 0, dark = 0;
  for (uint32_t t = 0; t < 2000; t += 10) {
    rh_game_lights(&g, t, &bright, &dim);
    if (bright == (1u << k0)) lit++;
    if (bright == 0) dark++;
  }
  check(lit > 100 && dark > 30, "Blink: the first key blinks before the song starts");
  rh_game_key(&g, k0, 3000, &note);
  int k1 = rh_key_of(g.tune.note[1]);
  rh_game_lights(&g, 3100, &bright, &dim);
  bool gap = bright == 0;
  rh_game_lights(&g, 3300, &bright, &dim);
  check(gap && bright == (1u << k1), "the next key lights a moment after a right one, so a repeated key shows");
  rh_game_key(&g, 25, 4000, &note);
  rh_game_lights(&g, 4100, &bright, &dim);
  check(bright == (1u << k1), "after a wrong key the right one is lit at once");

  r[RH_LIGHTS] = RH_AHEAD;
  rh_game_settings(&g, r);
  rh_game_lights(&g, 6300, &bright, &dim);
  int k2 = rh_key_of(g.tune.note[2]);
  check(dim == (k2 != k1 ? 1u << k2 : 0u), "Ahead: the key after next is lit dimly");

  row(3, RH_SOLO, r);
  rh_game_init(&g, r, 0);
  bool first = false;
  for (uint32_t t = 0; t < 640; t += 20) {
    rh_game_lights(&g, t, &bright, &dim);
    first = first || bright == 1u << rh_key_of(g.tune.note[0]);
  }
  rh_game_key(&g, rh_key_of(g.tune.note[0]), 500, &note);
  rh_game_lights(&g, 1500, &bright, &dim);
  bool hidden = bright == 0;
  bool shown = false;
  for (uint32_t t = 3000; t < 4000; t += 20) {
    rh_game_lights(&g, t, &bright, &dim);
    shown = shown || bright == 1u << rh_key_of(g.tune.note[1]);
  }
  check(first && hidden && shown, "Solo: the light helps only when the child waits");
  rh_game_key(&g, rh_key_of(g.tune.note[1]), 4000, &note);
  rh_game_key(&g, 26, 4300, &note);
  rh_game_lights(&g, 4400, &bright, &dim);
  check(bright == 1u << rh_key_of(g.tune.note[2]), "Solo: a wrong key brings the light");

  // Practising a line.
  row(3, RH_GUIDE, r);
  rh_game_init(&g, r, 0);
  rh_game_line(&g, 2, 100);
  int fromLine = g.pos;
  rh_game_line(&g, -1, 200);
  check(fromLine == 14 && g.pos == 7 && g.state == RH_READY && g.tune.line[g.pos] == 1,
        "LINE moves to the start of the line before or after");
  for (int i = g.pos; i < g.tune.count; i++) rh_game_key(&g, rh_key_of(g.tune.note[i]), 300 + i * 300, &note);
  check(g.state == RH_DONE && g.stars == 1, "a song started part way through earns one star");

  // The celebration: a fanfare, then the song again in full with its chords.
  g = playThrough(5, RH_GUIDE, 0, 0);
  uint32_t done = g.done_at;
  std::vector<int> ons;
  int off[RH_AUTO_MAX], on[RH_AUTO_MAX], no = 0, nn = 0, held = 0, most = 0, tune = 0, low = 0;
  bool encore = false;
  for (uint32_t t = done; t < done + 60000 && (g.state == RH_DONE || g.state == RH_ENCORE); t++) {
    rh_game_step(&g, t, off, &no, on, &nn);
    held += nn - no;
    most = std::max(most, held);
    encore = encore || g.state == RH_ENCORE;
    for (int i = 0; i < nn; i++) {
      ons.push_back(on[i]);
      if (g.state == RH_ENCORE && on[i] >= 55) tune++;
      if (on[i] < 52) low++;
    }
  }
  check(ons.size() >= 4 && ons[0] == 60 && ons[3] == 72, "a finished song plays a fanfare");
  check(encore && tune >= g.tune.count && low >= g.tune.chords && most >= 4 && most <= 5,
        "then the whole song again, with a bass and chords under the tune (" + std::to_string(ons.size()) +
            " notes, at most " + std::to_string(most) + " at once)");
  check(g.state == RH_READY && g.pos == 0 && held == 0 && g.auto_count == 0,
        "and lets every note go, ready to play again from the start");
  g = playThrough(5, RH_GUIDE, 0, 0);
  rh_game_lights(&g, g.done_at + 500, &bright, &dim);
  check(bright != 0, "the keys put on a light show for a finished song");
}

void listen() {
  int32_t r[RH_FIELDS];
  for (int s = 0; s < rh_song_count(); s++) {
    row(s, RH_GUIDE, r);
    rh_game_t g;
    rh_game_init(&g, r, 0);
    rh_game_listen(&g, 0);
    std::vector<int> ons;
    int off[RH_AUTO_MAX], on[RH_AUTO_MAX], no = 0, nn = 0, sounding = 0, worst = 0;
    bool lightsFollow = true;
    uint32_t t = 0, due = 0;
    for (; t < 120000 && g.state == RH_LISTEN; t++) {
      if (t < due) continue;
      rh_game_step(&g, t, off, &no, on, &nn);
      due = rh_game_due(&g, t);
      sounding += nn - no;
      worst = std::max(worst, sounding);
      for (int i = 0; i < nn; i++) ons.push_back(on[i]);
      uint32_t bright, dim;
      rh_game_lights(&g, t, &bright, &dim);
      if (nn && bright != 1u << rh_key_of(on[0])) lightsFollow = false;
    }
    bool same = (int)ons.size() == g.tune.count;
    for (size_t i = 0; same && i < ons.size(); i++) same = ons[i] == g.tune.note[i];
    if (!(same && worst == 1 && sounding == 0 && g.state == RH_READY && lightsFollow))
      printf("     %zu of %d notes, same %d, most at once %d, left %d, state %d, lights %d\n", ons.size(), g.tune.count,
             same, worst, sounding, g.state, lightsFollow);
    check(same && worst == 1 && sounding == 0 && g.state == RH_READY && lightsFollow,
          std::string(rh_song_title(s)) + ": plays itself note by note, its keys lit, then waits");
  }
  // The song's length follows its tempo and Speed.
  row(0, RH_GUIDE, r);
  rh_game_t g;
  rh_game_init(&g, r, 0);
  rh_game_listen(&g, 0);
  int off[RH_AUTO_MAX], on[RH_AUTO_MAX], no = 0, nn = 0;
  uint32_t t = 0, last = 0;
  for (; t < 60000 && g.state == RH_LISTEN; t++) {
    rh_game_step(&g, t, off, &no, on, &nn);
    if (nn) last = t;
  }
  // 17 notes at 96 BPM: the last note starts 600 ms and 28 eighths in.
  check(last == 600 + 28 * (3000000 / 9600), "a listened song keeps its tempo to the millisecond");
  rh_game_init(&g, r, 0);
  rh_game_listen(&g, 0);
  for (t = 0; t < 2000; t++) rh_game_step(&g, t, off, &no, on, &nn);
  int note = 0;
  rh_game_key(&g, rh_key_of(g.tune.note[0]), 2000, &note);
  rh_game_step(&g, 2000, off, &no, on, &nn);
  check(g.state == RH_PLAYING && g.pos == 1 && no == 1 && g.auto_count == 0,
        "a key stops a listened song, lets its note go and starts playing");
  // Through the engine, a wrong key sounds nothing.
  mf1_offline_init(kRate);
  rhymes_offline(1);
  rhymes_install();
  render(0.2);
  rhymes_offline_step(0);
  rh_default(2, r);
  rhymes_load(r);
  render(0.1);
  rh_tune(2, &g.tune);
  int wrongKey = rh_key_of(g.tune.note[0]) == 0 ? 1 : 0;
  int was = rhymes_key_down(wrongKey);
  auto quiet = render(0.3);
  rhymes_key_up(wrongKey);
  check(was == RH_WRONG && peak(quiet) < 1e-4, "a wrong key is silent through the engine");
  rhymes_offline(0);
}

void engine() {
  mf1_offline_init(kRate);
  rhymes_offline(1);
  rhymes_install();
  // Installing sends every voice to the engine; let it take them before anything else.
  render(0.2);
  for (int s = 0; s < rh_song_count(); s++) {
    int32_t r[RH_FIELDS];
    rh_default(s, r);
    rhymes_offline_step(0);
    rhymes_load(r);
    char name[11];
    mf1_edit_name(name);
    uint8_t v[155];
    rh_voice(rh_song_sound(s), v);
    bool named = memcmp(name, v + 145, 10) == 0 && mf1_current_preset() == s;
    render(0.3);
    rh_tune_t t;
    rh_tune(s, &t);
    int key = rh_key_of(t.note[0]);
    int res = rhymes_key_down(key);
    auto a = render(0.6);
    rhymes_key_up(key);
    auto b = render(2.5);
    double p = std::max(peak(a), peak(b)), on = rms(a, 0.28, 0.33), tail = rms(b, 2.0, 2.5), top = 0;
    for (double x = 0; x + 0.05 <= 0.6; x += 0.01) top = std::max(top, rms(a, x, x + 0.05));
    bool finite = true;
    for (float x : a) finite = finite && std::isfinite(x);
    char what[160];
    snprintf(what, sizeof what, "%s: its sound plays, loudest 50 ms %.1f dB, %.1f dB at 0.3 s, gone after key up",
             rh_song_title(s), 20 * std::log10(top), 20 * std::log10(on));
    check(named && res == RH_RIGHT && finite && p > 0.03 && p < 1.0 && top > 0.03 && top < 0.07 && tail < top * 0.1, what);
  }
  // Listening, through the engine.
  int32_t r[RH_FIELDS];
  rh_default(0, r);
  rhymes_offline_step(0);
  rhymes_load(r);
  rhymes_listen();
  uint64_t lo, hi;
  int heard = 0;
  for (uint32_t t = 0; t < 14000; t += 10) {
    rhymes_offline_step(t);
    render(0.01);
    mf1_sounding(&lo, &hi);
    if (lo || hi) heard++;
  }
  mf1_sounding(&lo, &hi);
  rh_game_t g;
  uint32_t now;
  rhymes_game(&g, &now);
  check(heard > 200 && !lo && !hi && g.state == RH_READY, "a listened song sounds through the engine, then stops");
  rhymes_offline(0);
}

uint32_t sceneHash(int song, const rh_view_t& v, int step) {
  std::vector<uint32_t> px(RH_SCENE_W * RH_SCENE_H, 0xDEADBEEF);
  rh_scene(song, &v, step, px.data());
  uint32_t h = 2166136261u;
  for (uint32_t c : px) {
    if (c == 0xDEADBEEF) return 0;
    h = (h ^ c) * 16777619u;
  }
  return h;
}

rh_view_t viewAt(int song, int pos, int done) {
  int32_t r[RH_FIELDS];
  rh_default(song, r);
  rh_game_t g = playThrough(song, RH_GUIDE, 0, 0);
  rh_game_init(&g, r, 0);
  int note = 0;
  uint32_t t = 0;
  for (int i = 0; i < pos; i++) rh_game_key(&g, rh_key_of(g.tune.note[i]), t += 300, &note);
  rh_view_t v;
  rh_game_view(&g, t + (done >= 0 ? done * RH_SCENE_MS : 2000), 0, &v);
  return v;
}

void pictures() {
  std::set<uint32_t> songs;
  for (int s = 0; s < rh_song_count(); s++) {
    rh_tune_t t;
    int n = rh_tune(s, &t);
    uint32_t start = sceneHash(s, viewAt(s, 0, -1), 0), half = sceneHash(s, viewAt(s, n / 2, -1), 0);
    uint32_t end = sceneHash(s, viewAt(s, n, 3), 0);
    check(start && half && end && start != half && half != end,
          std::string(rh_song_title(s)) + ": its picture is drawn whole and moves on as the song does");
    songs.insert(start);
  }
  check((int)songs.size() == rh_song_count(), "every song has a picture of its own");
  rh_view_t v = viewAt(0, 4, -1);
  check(v.pos == 4 && v.line == 0 && v.line_pos == 4 && v.line_len == 6 && v.hit == 20 && v.done < 0,
        "the picture's view: notes played, the line, and time since the last note");
}

}  // namespace

int rhymesTests() {
  failures = 0;
  songs();
  game();
  listen();
  engine();
  pictures();
  printf(failures ? "rhymes: %d failed\n" : "rhymes: all passed\n", failures);
  return failures;
}

// Every song's picture at the start, half way, and finished, as raw RGB at four times the size, a row each.
void rhymesSheet(const char* path) {
  const int k = 3, w = RH_SCENE_W * k, h = RH_SCENE_H * k, cols = 3, rows = rh_song_count();
  std::vector<uint8_t> rgb((size_t)w * cols * h * rows * 3, 0xFF);
  std::vector<uint32_t> px(RH_SCENE_W * RH_SCENE_H);
  for (int s = 0; s < rows; s++) {
    rh_tune_t t;
    int n = rh_tune(s, &t);
    for (int c = 0; c < cols; c++) {
      rh_view_t v = viewAt(s, c == 0 ? 0 : c == 1 ? n / 2 : n, c == 2 ? 5 : -1);
      if (c == 1) v.hit = 1;
      rh_scene(s, &v, 3, px.data());
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
          uint32_t col = px[(y / k) * RH_SCENE_W + x / k];
          uint8_t* o = &rgb[(((size_t)(s * h + y)) * w * cols + c * w + x) * 3];
          o[0] = (uint8_t)(col >> 16), o[1] = (uint8_t)(col >> 8), o[2] = (uint8_t)col;
        }
    }
  }
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fwrite(rgb.data(), 1, rgb.size(), f);
  fclose(f);
  printf("%d x %d\n", w * cols, h * rows);
}
