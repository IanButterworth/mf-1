// Rhymes on MF-1: puts the songs' sounds in the engine, and runs the game behind the panel's keys.
//
// The game plays notes by itself (a song listened to, the fanfare), so it runs on a thread of its own that sleeps
// until the next of those notes is due. Only that thread and the main thread take the lock; the audio thread never
// sees it.
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#include "MF1Core.h"
#include "RhymesCore.h"

namespace {

void setFx(int cc, int value) {
  if (mf1_fx_get(cc) != value) mf1_fx_set(cc, value);
}

void setEdit(int index, int value) {
  if (mf1_edit_get(index) != value) mf1_edit_set(index, value);
}

void packVoice(const uint8_t v[155], uint8_t out[128]) {
  for (int op = 0; op < 6; op++) {
    const uint8_t* p = v + op * 21;
    uint8_t* o = out + op * 17;
    memcpy(o, p, 11);
    o[11] = (uint8_t)((p[12] << 2) | p[11]);
    o[12] = (uint8_t)((p[20] << 3) | p[13]);
    o[13] = (uint8_t)((p[15] << 2) | p[14]);
    o[14] = p[16];
    o[15] = (uint8_t)((p[18] << 1) | p[17]);
    o[16] = p[19];
  }
  memcpy(out + 102, v + 126, 8);
  out[110] = v[134];
  out[111] = (uint8_t)((v[136] << 3) | v[135]);
  out[112] = v[137];
  out[113] = v[138];
  out[114] = v[139];
  out[115] = v[140];
  out[116] = (uint8_t)((v[143] << 4) | (v[142] << 1) | v[141]);
  out[117] = v[144];
  memcpy(out + 118, v + 145, 10);
}

// F0 43 0n 09 20 00, 32 voices, checksum, F7. Slots past the songs repeat them.
void bankDump(uint8_t out[4104]) {
  out[0] = 0xF0, out[1] = 0x43, out[2] = 0x00, out[3] = 0x09, out[4] = 0x20, out[5] = 0x00;
  for (int i = 0; i < 32; i++) {
    uint8_t v[155];
    rh_voice(rh_song_sound(i % rh_song_count()), v);
    packVoice(v, out + 6 + i * 128);
  }
  int sum = 0;
  for (int i = 0; i < 4096; i++) sum += out[6 + i];
  out[6 + 4096] = (uint8_t)((128 - (sum & 0x7f)) & 0x7f);
  out[6 + 4097] = 0xF7;
}

struct Player {
  std::mutex lock;
  std::condition_variable wake;
  std::thread thread;
  bool started = false;
  bool offline = false;
  uint32_t offlineNow = 0;
  bool loaded = false;
  int song = -1;
  rh_game_t game{};
  // The note each key plays now, and each of the game's own notes, as sent: at the octave it started at.
  int keyNote[RH_KEYS];
  int autoSent[128];
  int refs[128] = {};
  uint32_t generation = 0;

  Player() {
    for (int& k : keyNote) k = -1;
    for (int& a : autoSent) a = -1;
  }

  uint32_t now() const {
    if (offline) return offlineNow;
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  void press(int note) {
    if (note < 0 || note > 127) return;
    if (refs[note]++ == 0) mf1_key_down(note);
  }

  void release(int note) {
    if (note < 0 || note > 127 || refs[note] == 0) return;
    if (--refs[note] == 0) mf1_key_up(note);
  }

  // Plays what the game has due. Called with the lock held.
  void step(uint32_t t) {
    int off[RH_AUTO_MAX], on[RH_AUTO_MAX], offs = 0, ons = 0;
    int before = game.state * 1000 + game.pos;
    rh_game_step(&game, t, off, &offs, on, &ons);
    for (int i = 0; i < offs; i++) {
      int n = off[i];
      if (n >= 0 && n < 128 && autoSent[n] >= 0) {
        release(autoSent[n]);
        autoSent[n] = -1;
      }
    }
    const int octave = mf1_global_get(MF1_G_OCTAVE);
    for (int i = 0; i < ons; i++) {
      int n = on[i];
      if (n < 0 || n > 127) continue;
      if (autoSent[n] >= 0) release(autoSent[n]);
      autoSent[n] = n + 12 * octave;
      press(autoSent[n]);
    }
    if (ons || offs || before != game.state * 1000 + game.pos) generation++;
  }

  void run() {
    std::unique_lock<std::mutex> l(lock);
    for (;;) {
      uint32_t t = now();
      if (loaded) step(t);
      uint32_t due = loaded ? rh_game_due(&game, t) : t + 1000;
      uint32_t wait = due - t;
      if (wait > 50) wait = 50;
      wake.wait_for(l, std::chrono::milliseconds(wait));
    }
  }

  void ensureThread() {
    if (started || offline) return;
    started = true;
    thread = std::thread([this] { run(); });
    thread.detach();
  }

  void releaseKeys() {
    for (int& k : keyNote)
      if (k >= 0) {
        release(k);
        k = -1;
      }
  }

  void releaseAll() {
    releaseKeys();
    for (int& a : autoSent)
      if (a >= 0) {
        release(a);
        a = -1;
      }
    game.auto_count = 0;
  }
};

Player& P() {
  static Player* p = new Player();
  return *p;
}

void applyLocked(Player& p, const int32_t row[RH_FIELDS]) {
  int32_t r[RH_FIELDS];
  memcpy(r, row, sizeof(r));
  rh_clamp(r);
  uint8_t v[155];
  rh_voice(r[RH_SOUND], v);
  for (int i = 0; i < 145; i++) setEdit(i, v[i]);
  if (mf1_edit_mono()) mf1_edit_set_mono(false);
  if (mf1_env_get(0) != 0) mf1_env_set(0, 0);
  if (mf1_arp_on()) mf1_arp_enable(false);
  if (mf1_seq_on()) mf1_seq_enable(false);
  int cc[24], value[24];
  int n = rh_fx(r, cc, value);
  for (int i = 0; i < n; i++) setFx(cc[i], value[i]);
  if (!p.loaded || p.song != r[RH_SONG]) {
    p.releaseAll();
    rh_game_init(&p.game, r, p.now());
    p.song = r[RH_SONG];
    p.loaded = true;
  } else {
    rh_game_settings(&p.game, r);
  }
  p.generation++;
}

}  // namespace

void rhymes_export_bank(uint8_t out[4104]) { bankDump(out); }

void rhymes_install(void) {
  uint8_t dump[4104];
  bankDump(dump);
  for (int b = 0; b < 4; b++) mf1_import_factory_bank(b, dump, sizeof(dump));
}

void rhymes_load(const int32_t row[RH_FIELDS]) {
  Player& p = P();
  int32_t r[RH_FIELDS];
  memcpy(r, row, sizeof(r));
  rh_clamp(r);
  std::lock_guard<std::mutex> l(p.lock);
  mf1_load_preset(r[RH_SONG]);
  applyLocked(p, r);
  p.ensureThread();
  p.wake.notify_one();
}

void rhymes_apply(const int32_t row[RH_FIELDS]) {
  Player& p = P();
  std::lock_guard<std::mutex> l(p.lock);
  applyLocked(p, row);
  p.wake.notify_one();
}

int rhymes_key_down(int key) {
  Player& p = P();
  if (key < 0 || key >= RH_KEYS) return RH_FREE;
  std::lock_guard<std::mutex> l(p.lock);
  if (!p.loaded || p.keyNote[key] >= 0) return RH_FREE;
  uint32_t t = p.now();
  int note = 0;
  int r = rh_game_key(&p.game, key, t, &note);
  // A key that stops a song playing itself lets its note go first, so the key's own note is never cut.
  p.step(t);
  int sent = note + 12 * mf1_global_get(MF1_G_OCTAVE);
  if (note >= 0 && sent >= 0 && sent <= 127) {
    p.keyNote[key] = sent;
    p.press(sent);
  }
  p.generation++;
  p.wake.notify_one();
  return r;
}

void rhymes_key_up(int key) {
  Player& p = P();
  if (key < 0 || key >= RH_KEYS) return;
  std::lock_guard<std::mutex> l(p.lock);
  if (p.keyNote[key] >= 0) {
    p.release(p.keyNote[key]);
    p.keyNote[key] = -1;
  }
}

void rhymes_all_keys_up(void) {
  Player& p = P();
  std::lock_guard<std::mutex> l(p.lock);
  p.releaseKeys();
}

void rhymes_restart(void) {
  Player& p = P();
  std::lock_guard<std::mutex> l(p.lock);
  if (!p.loaded) return;
  uint32_t t = p.now();
  rh_game_restart(&p.game, t);
  p.step(t);
  p.generation++;
  p.wake.notify_one();
}

void rhymes_line(int delta) {
  Player& p = P();
  std::lock_guard<std::mutex> l(p.lock);
  if (!p.loaded) return;
  uint32_t t = p.now();
  rh_game_line(&p.game, delta, t);
  p.step(t);
  p.generation++;
  p.wake.notify_one();
}

void rhymes_listen(void) {
  Player& p = P();
  std::lock_guard<std::mutex> l(p.lock);
  if (!p.loaded) return;
  uint32_t t = p.now();
  rh_game_listen(&p.game, t);
  p.step(t);
  p.generation++;
  p.wake.notify_one();
}

void rhymes_game(rh_game_t* out, uint32_t* now) {
  Player& p = P();
  std::lock_guard<std::mutex> l(p.lock);
  *out = p.game;
  *now = p.now();
}

uint32_t rhymes_generation(void) {
  Player& p = P();
  std::lock_guard<std::mutex> l(p.lock);
  return p.generation;
}

void rhymes_offline(int on) {
  Player& p = P();
  std::lock_guard<std::mutex> l(p.lock);
  p.offline = on != 0;
}

void rhymes_offline_step(uint32_t now) {
  Player& p = P();
  std::lock_guard<std::mutex> l(p.lock);
  p.offlineNow = now;
  if (p.loaded) p.step(now);
}
