// The player: runs the process on its own thread and presses the engine's keys when each note falls due.
//
// The thread sleeps until the next note on or off is due, at real-time priority, so a note leaves
// within a fraction of a millisecond of its time. The engine applies it at the start of the next audio buffer.
// Only this thread and the main thread take the player's lock; the audio thread never sees it.
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

#include "MF1Core.h"
#include "PhaseCore.h"

namespace {

// The engine's keys: the most notes the process can hold at once, and how close two starts of the same note may be
// and still count as one stroke.
constexpr int kMaxOffs = 64;
constexpr uint64_t kUnisonNs = 15'000'000;

struct Off {
  uint64_t ns;
  int note;
};

struct Player {
  std::mutex lock;
  std::thread thread;
  std::atomic<bool> running{false};
  bool offline = false;
  uint64_t offlineNow = 0;

  bool playing = false;
  PhaseProc proc{};
  // A tick and its time, from which every other tick's time is worked out, and how long a tick is.
  uint64_t originNs = 0;
  int64_t originTick = 0;
  double nsPerTick = 1;
  // Notes before this tick have been started.
  int64_t scanTick = 0;
  int pendingCount = 0;
  PhaseNote pending[PH_MAX_PARTS];

  Off offs[kMaxOffs];
  int offCount = 0;
  int refs[128] = {};
  uint64_t lastOn[128] = {};

  // Timing: notes and offs counted, with their total and worst lateness in microseconds.
  uint64_t wakes = 0;
  double lateSum = 0, lateMax = 0;

  uint64_t now() const {
    if (offline) return offlineNow;
    static mach_timebase_info_data_t tb = [] {
      mach_timebase_info_data_t t;
      mach_timebase_info(&t);
      return t;
    }();
    return mach_absolute_time() * tb.numer / tb.denom;
  }

  static uint64_t toMach(uint64_t ns) {
    static mach_timebase_info_data_t tb = [] {
      mach_timebase_info_data_t t;
      mach_timebase_info(&t);
      return t;
    }();
    return ns * tb.denom / tb.numer;
  }

  static double tickNs(int tempo) { return 60e9 / (tempo * 4.0) / PH_TPS; }

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

  void noteLate(uint64_t due, uint64_t t) {
    if (offline) return;
    double late = t > due ? (double)(t - due) / 1000.0 : 0;
    wakes++;
    lateSum += late;
    lateMax = std::max(lateMax, late);
  }

  void release(int note) {
    if (refs[note] <= 0) return;
    if (--refs[note] == 0) mf1_key_up(note);
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

  void fireOn(const PhaseNote& n) {
    uint64_t at = timeOf(n.tick);
    press(n.note, at);
    if (offCount == kMaxOffs) fireOff(soonestOff());
    offs[offCount++] = Off{at + (uint64_t)(n.length * nsPerTick), n.note};
  }

  // Finds the next notes, looking a few steps ahead at a time; when a long stretch is silent, the search goes on at
  // the next wake.
  void findNext() {
    if (pendingCount || !playing) return;
    const int64_t window = 8 * PH_TPS;
    for (int tries = 0; tries < 8; tries++) {
      pendingCount = phase_next_notes(&proc, scanTick, scanTick + window, pending);
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

  void stopNotes() {
    playing = false;
    pendingCount = 0;
    while (offCount) fireOff(offCount - 1);
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
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
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

void phase_start(const PhaseProc* proc, double lead) {
  Player& p = P();
  p.startThread();
  std::lock_guard<std::mutex> g(p.lock);
  p.stopNotes();
  p.proc = *proc;
  p.nsPerTick = Player::tickNs(proc->tempo);
  p.originTick = 0;
  p.originNs = p.now() + (uint64_t)(std::max(0.0, lead) * 1e9);
  p.scanTick = 0;
  p.playing = true;
  if (p.offline) p.fireDue(p.offlineNow);
}

void phase_change(const PhaseProc* proc) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  if (!p.playing) {
    p.proc = *proc;
    return;
  }
  const uint64_t t = p.now();
  if (proc->tempo != p.proc.tempo) {
    // The next tick keeps its time, and the ticks after it run at the new tempo.
    int64_t next = p.tickAt(t) + 1;
    p.originNs = p.timeOf(next);
    p.originTick = next;
    p.nsPerTick = Player::tickNs(proc->tempo);
  }
  if (!phase_proc_same_motion(proc, &p.proc)) {
    // Restart on the next step of part 1, so the beat carries on.
    int64_t step = (int64_t)std::floor((double)p.tickAt(t) / PH_TPS) + 1;
    p.originNs = p.timeOf(step * PH_TPS);
    p.originTick = 0;
    p.scanTick = 0;
  }
  p.proc = *proc;
  p.pendingCount = 0;
}

void phase_stop(void) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  p.stopNotes();
}

bool phase_playing(void) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  return p.playing;
}

bool phase_now(PhaseView* out) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  if (!p.playing) return false;
  phase_view(&p.proc, std::max<int64_t>(0, p.tickAt(p.now())), out);
  return true;
}

void phase_key_down(int note) {
  if (note < 0 || note > 127) return;
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  // A key always strikes, even when the process holds the note.
  mf1_key_down(note);
  p.lastOn[note] = p.now();
  p.refs[note]++;
}

void phase_key_up(int note) {
  if (note < 0 || note > 127) return;
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  p.release(note);
}

void phase_shutdown(void) {
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

int phase_refs(int note) {
  if (note < 0 || note > 127) return 0;
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  return p.refs[note];
}

void phase_timing(double out[3]) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  out[0] = (double)p.wakes;
  out[1] = p.wakes ? p.lateSum / p.wakes : 0;
  out[2] = p.lateMax;
}

void phase_offline(bool on) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  p.offline = on;
  p.offlineNow = 0;
}

void phase_offline_seek(double seconds) {
  Player& p = P();
  std::lock_guard<std::mutex> g(p.lock);
  if (!p.offline) return;
  p.offlineNow = (uint64_t)std::llround(std::max(0.0, seconds) * 1e9);
  p.fireDue(p.offlineNow);
}
