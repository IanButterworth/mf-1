#pragma once
#include <atomic>
#include <cstddef>

namespace mf1 {

// Single producer, single consumer ring. Neither side blocks or allocates,
// so the audio thread can drain it safely.
template <typename T, size_t Capacity>
class SpscQueue {
  static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

 public:
  bool push(const T& v) {
    size_t w = write_.load(std::memory_order_relaxed);
    size_t r = read_.load(std::memory_order_acquire);
    if (w - r >= Capacity) return false;
    items_[w & (Capacity - 1)] = v;
    write_.store(w + 1, std::memory_order_release);
    return true;
  }

  bool pop(T& out) {
    size_t r = read_.load(std::memory_order_relaxed);
    size_t w = write_.load(std::memory_order_acquire);
    if (r == w) return false;
    out = items_[r & (Capacity - 1)];
    read_.store(r + 1, std::memory_order_release);
    return true;
  }

 private:
  T items_[Capacity];
  alignas(64) std::atomic<size_t> write_{0};
  alignas(64) std::atomic<size_t> read_{0};
};

}  // namespace mf1
