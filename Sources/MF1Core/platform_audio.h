#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace mf1 {

// An output device through an AUHAL unit: the system default, or one chosen
// by UID. The render function runs on the Core Audio thread; right is null
// on a mono device.
class AudioOutput {
 public:
  using RenderFn = std::function<void(float* left, float* right, uint32_t frames)>;
  using PrepareFn = std::function<void(double sampleRate)>;
  ~AudioOutput();
  // Called with the device rate before the first render.
  void onPrepare(PrepareFn fn) { prepare_ = std::move(fn); }
  // Called on a background queue when a chosen device disappears.
  void onDeviceLost(std::function<void()> fn) { lost_ = std::move(fn); }
  // An empty UID follows the system default output, as does a chosen
  // device that is missing or cannot be selected.
  bool start(RenderFn fn, const std::string& deviceUID = "");
  void stop();
  // True while the device renders: started, and called back recently or
  // just started. Safe from any thread.
  bool running() const;
  struct Device {
    std::string uid, name;
  };
  static std::vector<Device> outputDevices();
  void render(float* left, float* right, uint32_t frames);
  double sampleRate() const { return sampleRate_; }
  // Output path delay as Core Audio reports it, in frames: the IO buffer,
  // plus device, safety offset and stream latencies.
  struct Latency {
    uint32_t bufferFrames = 0, deviceFrames = 0, safetyFrames = 0, streamFrames = 0;
  };
  Latency latency() const;

 private:
  friend struct AudioListeners;
  void deviceChanged();
  void checkAlive();
  void* unit_ = nullptr;
  RenderFn fn_;
  PrepareFn prepare_;
  std::function<void()> lost_;
  double sampleRate_ = 48000;
  // Guards the unit against the listeners, which run on other threads.
  std::mutex mu_;
  // A chosen device, watched in case it is unplugged.
  uint32_t watched_ = 0;
  std::atomic<bool> started_{false};
  std::atomic<uint64_t> startedAt_{0}, lastRender_{0};
};

}  // namespace mf1
