#pragma once
#include <cstdint>
#include <functional>
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
  // An empty UID follows the system default output.
  bool start(RenderFn fn, const std::string& deviceUID = "");
  void stop();
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
  void* unit_ = nullptr;
  RenderFn fn_;
  PrepareFn prepare_;
  double sampleRate_ = 48000;
};

}  // namespace mf1
