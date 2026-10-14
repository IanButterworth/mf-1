#pragma once
#include <cstdint>
#include <functional>

namespace mf1 {

// Default output device through an AUHAL unit. The render function runs on
// the Core Audio thread; right is null on a mono device.
class AudioOutput {
 public:
  using RenderFn = std::function<void(float* left, float* right, uint32_t frames)>;
  using PrepareFn = std::function<void(double sampleRate)>;
  ~AudioOutput();
  // Called with the device rate before the first render.
  void onPrepare(PrepareFn fn) { prepare_ = std::move(fn); }
  bool start(RenderFn fn);
  void stop();
  void render(float* left, float* right, uint32_t frames);
  double sampleRate() const { return sampleRate_; }

 private:
  void* unit_ = nullptr;
  RenderFn fn_;
  PrepareFn prepare_;
  double sampleRate_ = 48000;
};

}  // namespace mf1
