// The player: runs the lanes on its own thread and presses the engine's keys when each note falls due.
//
// The thread sleeps until the next note on or off is due, at real-time priority, so a note leaves within a
// fraction of a millisecond of its time. A key that starts the clock plays its first note at once, from the main
// thread, rather than waiting for the thread to wake. The engine applies a note at the start of the next audio
// buffer. Only this thread and the main thread take the player's lock; the audio thread never sees it.
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

#include "ArpeggiCore.h"
#include "MF1Core.h"

namespace {

// The most notes the lanes can hold at once, and how close two starts of the same note may be and still count as
// one strike.
constexpr int kMaxOffs = 96;
constexpr uint64_t kUnisonNs = 8'000'000;
constexpr int kRecent = 96;

struct Off {
  uint64_t ns;
  int note;
};

struct Hit {
  uint64_t ns, lengthNs;
  int lane, note;
};

struct Player {
  std::mutex lock;
  std::thread thread;
  std::atomic<bool> running{false};
  bool offline = false;
  uint64_t offlineNow = 0;

  bool playing = false;
  ApProc proc{};
  ApPool pool{};
  // A tick and its time, from which every other tick's time is worked out, and how long a tick is.
  uint64_t originNs = 0;
  int64_t originTick = 0;
  double nsPerTick = 1;
  // Notes before this tick have been started.
  int64_t scanTick = 0;
  int pendingCount = 0;
  ApNote pending[AP_MAX_OUT];

  Off offs[kMaxOffs];
  int offCount = 0;
  int refs[128] = {};
  uint64_t lastOn[128] = {};

  Hit recent[kRecent];
  int recentNext = 0, recentCount = 0;

  uint64_t wakes = 0;
  double lateSum = 0, lateMax = 0;

  Player() {
    ap_pool_init(&pool, AP_LATCH, 4);
    proc.tempo = 120, proc.span = 1, proc.gate[0] = proc.gate[1] = 50;
  }

  static const mach_timebase_info_data_t& timebase() {
    static mach_timebase_info_data_t tb = [] {
      mach_timebase_info_data_t t;
      mach_timebase_info(&t);
      return t;
    }();
    return tb;
  }

  uint64_t now() const {
    if (offline) return offlineNow;
    return mach_absolute_time() * timebase().numer / timebase().denom;
  }

  static uint64_t toMach(uint64_t ns) { return ns * timebase().denom / timebase().numer; }

  static double tickNs(int tempo) { return 60e9 / std::max(1, tempo) / AP_PPQ; }

  uint64_t timeOf(int64_t tick) const {
    double t = (double)originNs + (double)(tick - originTick) * nsPerTick;
    return t <= 0 ? 0 : (uint64_t)t;
  }

  int64_t tickAt(uint64_t ns) const {
    return originTick + (int64_t)std::floor(((double)ns - (double)originNs) / nsPerTick);
  }

  void press(int note, uint64_t at) {
    if (refs[note] == 0 || at - lastOn[note] >= kUnisonNs) {
      mf1_key_down(note);
      lastOn[note] = at;
    }
    refs[note]++;
  }

  void release(int note) {
    if (refs[note] <= 0) return;
    if (--refs[note] == 0) mf1_key_up(note);
  }

  void noteLate(uint64_t due, uint64_t t) {
    if (offline) return;
    double late = t > due ? (double)(t - due) / 1000.0 : 0;
    wakes++;
    lateSum += late;
    lateMax = std::max(lateMax, late);
  }

  void fireOff(int i) {
    int note = offs[i].note;
    offs[i] = offs[--offCount];
    release(note);
  }

  int soonestOff() const {
    int best = -1;
    for (int i = 0; i < offCount; i++)
      if (best < 0 || offs[i].ns < offs[best].ns) best = i;
    return best;
  }

  void fireOn(const ApNote& n) {
    uint64_t at = timeOf(n.tick);
    uint64_t length = (uint64_t)(n.length * nsPerTick);
    press(n.note, at);
    if (offCount == kMaxOffs) fireOff(soonestOff());
    offs[offCount++] = Off{at + length, n.note};
    recent[recentNext] = Hit{at, length, n.lane, n.note};
    recentNext = (recentNext + 1) % kRecent;
    recentCount = std::min(kRecent, recentCount + 1);
  }

  // Finds the next notes, looking two bars ahead at a time; when a long stretch is silent, the search goes on at
  // the next wake.
  void findNext() {
    if (pendingCount || !playing) return;
    const int64_t window = 8 * (int64_t)AP_PPQ;
    for (int tries = 0; tries < 4; tries++) {
      pendingCount = ap_next_notes(&proc, &pool, scanTick, scanTick + window, pending);
      if (pendingCount) return;
      scanTick += window;
    }
  }

  // Fires everything due by `t`, offs first at the same time, and returns when the next thing is due.
  uint64_t fireDue(uint64_t t) {
    for (;;) {
      findNext();
      int oi = soonestOff();
      uint64_t on = pendingCount ? timeOf(pending[0].tick) : playing ? timeOf(scanTick) : UINT64_MAX;
      uint64_t off = oi >= 0 ? offs[oi].ns : UINT64_MAX;
      if (std::min(on, off) > t) return std::min(on, off);
      if (off <= on) {
        noteLate(off, now());
        fireOff(oi);
      } else if (pendingCount) {
        noteLate(on, now());
        for (int i = 0; i < pendingCount; i++) fireOn(pending[i]);
        scanTick = pending[0].tick + 1;
        pendingCount = 0;
      } else {
        return t + 1'000'000;
      }
    }
  }

  // The lanes' notes stop; the clock stops.
  void stopNotes() {
    playing = false;
    pendingCount = 0;
    while (offCount) fireOff(offCount - 1);
  }

  // The clock starts at tick 0 now, and anything due is played at once.
  void startClock() {
    startThread();
    nsPerTick = tickNs(proc.tempo);
    originTick = 0;
    originNs = now();
    scanTick = 0;
    pendingCount = 0;
    playing = true;
    fireDue(now());
  }

  // The pool changed: the notes found ahead may be wrong now.
  void poolChanged() {
    pendingCount = 0;
    // Hold: with every key up, the clock stops, and notes already playing run out by themselves.
    if (playing && pool.count == 0) playing = false;
  }

  void startThread() {
    if (offline || running.exchange(true)) return;
    thread = std::thread([this] {
      setRealtime();
      while (running.load()) {
        uint64_t wake;
        {
          std::lock_guard<std::mutex> g(lock);
          uint64_t t = now();
          bool busy = playing || offCount;
          uint64_t next = busy ? fireDue(t) : t + 10'000'000;
          // Wake at least every few milliseconds, so a stop or a change is heard at once.
          wake = std::min(next, t + 4'000'000);
        }
        mach_wait_until(toMach(wake));
      }
    });
  }

  static void setRealtime() {
    const mach_timebase_info_data_t& tb = timebase();
    auto abs = [&](double ns) { return (uint32_t)(ns * tb.denom / tb.numer); };
    thread_time_constraint_policy_data_t policy;
    policy.period = abs(1e6);
    policy.computation = abs(0.2e6);
    policy.constraint = abs(0.5e6);
    policy.preemptible = 1;
    thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                      (thread_policy_t)&policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
  }
};

Player& P() {
  static Player* p = new Player;
  return *p;
}

}  // namespace

void arpeggi_set(const ApProc* proc) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  if (p.playing && proc->tempo != p.proc.tempo) {
    // The next tick keeps its time, and the ticks after it run at the new tempo.
    const uint64_t t = p.now();
    int64_t next = p.tickAt(t) + 1;
    p.originNs = p.timeOf(next);
    p.originTick = next;
    p.nsPerTick = Player::tickNs(proc->tempo);
  }
  p.proc = *proc;
  p.pendingCount = 0;
}

void arpeggi_pool_mode(int hold, int ring) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  if (p.pool.hold == hold && p.pool.ring == ring) return;
  ap_pool_mode(&p.pool, hold, ring);
  p.poolChanged();
}

void arpeggi_key_down(int note) {
  if (note < 0 || note > 127) return;
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  ap_pool_key_down(&p.pool, note);
  p.poolChanged();
  if (!p.playing && p.pool.count > 0) p.startClock();
}

void arpeggi_key_up(int note) {
  if (note < 0 || note > 127) return;
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  ap_pool_key_up(&p.pool, note);
  p.poolChanged();
}

void arpeggi_play_down(int note) {
  if (note < 0 || note > 127) return;
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  // A key played along always strikes, even when a lane holds the note.
  mf1_key_down(note);
  p.lastOn[note] = p.now();
  p.refs[note]++;
}

void arpeggi_play_up(int note) {
  if (note < 0 || note > 127) return;
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  p.release(note);
}

void arpeggi_seed(const int8_t* notes, int n) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  ap_pool_seed(&p.pool, notes, n);
  p.poolChanged();
  if (!p.playing && p.pool.count > 0) p.startClock();
}

bool arpeggi_start(void) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  p.stopNotes();
  if (p.pool.count == 0) return false;
  p.startClock();
  return true;
}

void arpeggi_stop(void) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  p.stopNotes();
}

void arpeggi_clear(void) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  p.stopNotes();
  ap_pool_clear(&p.pool);
}

bool arpeggi_playing(void) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  return p.playing;
}

void arpeggi_pool(ApPool* out) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  *out = p.pool;
}

bool arpeggi_now(int64_t* tick) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  if (!p.playing) return false;
  *tick = std::max<int64_t>(0, p.tickAt(p.now()));
  return true;
}

int arpeggi_recent(ApHit* out, int max) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  const uint64_t t = p.now();
  int n = 0;
  for (int i = 0; i < p.recentCount && n < max; i++) {
    const Hit& h = p.recent[(p.recentNext - 1 - i + kRecent) % kRecent];
    // Notes found ahead are not yet in the log, so every hit here has started.
    const double ago = t >= h.ns ? (double)(t - h.ns) / 1e6 : 0;
    out[n].lane = h.lane, out[n].note = h.note;
    out[n].ago_ms = (int32_t)std::min(ago, 1e9);
    out[n].length_ms = (int32_t)std::min((double)h.lengthNs / 1e6, 1e9);
    n++;
  }
  return n;
}

void arpeggi_shutdown(void) {
  Player& p = P();
  {
    std::lock_guard<std::mutex> g(p.lock);
    p.stopNotes();
    for (int n = 0; n < 128; n++)
      if (p.refs[n] > 0) {
        p.refs[n] = 0;
        mf1_key_up(n);
      }
  }
  if (p.running.exchange(false) && p.thread.joinable()) p.thread.join();
  mf1_all_notes_off();
}

int arpeggi_refs(int note) {
  if (note < 0 || note > 127) return 0;
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  return p.refs[note];
}

void arpeggi_timing(double out[3]) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  out[0] = (double)p.wakes;
  out[1] = p.wakes ? p.lateSum / p.wakes : 0;
  out[2] = p.lateMax;
}

void arpeggi_offline(bool on) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  p.offline = on;
  p.offlineNow = 0;
}

void arpeggi_offline_seek(double seconds) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  if (!p.offline) return;
  p.offlineNow = (uint64_t)std::llround(std::max(0.0, seconds) * 1e9);
  p.fireDue(p.offlineNow);
}
