#include "platform_audio.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <mach/mach_time.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace mf1 {

namespace {

OSStatus renderCallback(void* refCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32,
                        UInt32 frames, AudioBufferList* io) {
  auto* self = static_cast<AudioOutput*>(refCon);
  float* left = static_cast<float*>(io->mBuffers[0].mData);
  float* right = io->mNumberBuffers > 1 ? static_cast<float*>(io->mBuffers[1].mData) : nullptr;
  self->render(left, right, frames);
  return noErr;
}

}  // namespace

AudioOutput::~AudioOutput() { stop(); }

namespace {
std::string cfString(CFStringRef s) {
  if (!s) return "";
  char buf[512];
  if (!CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) return "";
  return buf;
}

AudioDeviceID deviceForUID(const std::string& uid) {
  for (AudioDeviceID id : [] {
         AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                      kAudioObjectPropertyElementMain};
         UInt32 sz = 0;
         AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &sz);
         std::vector<AudioDeviceID> ids(sz / sizeof(AudioDeviceID));
         AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &sz, ids.data());
         return ids;
       }()) {
    AudioObjectPropertyAddress ua{kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
    CFStringRef s = nullptr;
    UInt32 sz = sizeof(s);
    if (AudioObjectGetPropertyData(id, &ua, 0, nullptr, &sz, &s) == noErr && s) {
      bool match = cfString(s) == uid;
      CFRelease(s);
      if (match) return id;
    }
  }
  return 0;
}
}  // namespace

std::vector<AudioOutput::Device> AudioOutput::outputDevices() {
  std::vector<Device> out;
  AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                               kAudioObjectPropertyElementMain};
  UInt32 sz = 0;
  AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &sz);
  std::vector<AudioDeviceID> ids(sz / sizeof(AudioDeviceID));
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &sz, ids.data());
  for (AudioDeviceID id : ids) {
    AudioObjectPropertyAddress sa{kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                                  kAudioObjectPropertyElementMain};
    UInt32 ssz = 0;
    if (AudioObjectGetPropertyDataSize(id, &sa, 0, nullptr, &ssz) != noErr || ssz == 0) continue;
    Device d;
    CFStringRef s = nullptr;
    UInt32 csz = sizeof(s);
    AudioObjectPropertyAddress na{kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(id, &na, 0, nullptr, &csz, &s) == noErr && s) {
      d.name = cfString(s);
      CFRelease(s);
    }
    AudioObjectPropertyAddress ua{kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
    csz = sizeof(s);
    if (AudioObjectGetPropertyData(id, &ua, 0, nullptr, &csz, &s) == noErr && s) {
      d.uid = cfString(s);
      CFRelease(s);
    }
    if (!d.uid.empty()) out.push_back(d);
  }
  return out;
}

namespace {

// Asks for one 64 frame engine block per IO cycle. Notes start on block
// boundaries, so a smaller buffer would not make them start sooner.
void requestSmallBuffer(AudioUnit unit) {
  AudioDeviceID device = 0;
  UInt32 size = sizeof(device);
  AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device, &size);
  UInt32 frames = 64;
  AudioValueRange range{};
  AudioObjectPropertyAddress ra{kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal,
                                kAudioObjectPropertyElementMain};
  size = sizeof(range);
  if (device && AudioObjectGetPropertyData(device, &ra, 0, nullptr, &size, &range) == noErr)
    frames = (UInt32)std::max<Float64>(range.mMinimum, std::min<Float64>(range.mMaximum, frames));
  AudioUnitSetProperty(unit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0, &frames, sizeof(frames));
}

AudioUnit newOutputUnit(AudioDeviceID chosen) {
  AudioComponentDescription desc{};
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = chosen ? kAudioUnitSubType_HALOutput : kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
  AudioUnit unit = nullptr;
  if (!comp || AudioComponentInstanceNew(comp, &unit) != noErr) return nullptr;
  if (chosen &&
      AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &chosen,
                           sizeof(chosen)) != noErr) {
    AudioComponentInstanceDispose(unit);
    return nullptr;
  }
  return unit;
}

const AudioObjectPropertyAddress kAliveAddress{kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal,
                                               kAudioObjectPropertyElementMain};
const AudioObjectPropertyAddress kDefaultAddress{kAudioHardwarePropertyDefaultOutputDevice,
                                                 kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};

uint64_t nanosToTicks(uint64_t ns) {
  static mach_timebase_info_data_t tb = [] {
    mach_timebase_info_data_t t;
    mach_timebase_info(&t);
    return t;
  }();
  return ns * tb.denom / tb.numer;
}

}  // namespace

// The listeners hand their work to a short-lived thread: Core Audio may
// wait for a running listener while removing it, so they must not lock.
struct AudioListeners {
  static OSStatus alive(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* refCon) {
    auto* self = static_cast<AudioOutput*>(refCon);
    std::thread([self] { self->checkAlive(); }).detach();
    return noErr;
  }
  static OSStatus defaultChanged(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* refCon) {
    auto* self = static_cast<AudioOutput*>(refCon);
    std::thread([self] {
      // Give the unit time to follow the new default device first.
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      self->deviceChanged();
    }).detach();
    return noErr;
  }
  static void unitDevice(void* refCon, AudioUnit, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement) {
    auto* self = static_cast<AudioOutput*>(refCon);
    std::thread([self] { self->deviceChanged(); }).detach();
  }
};

bool AudioOutput::start(RenderFn fn, const std::string& deviceUID) {
  fn_ = std::move(fn);
  // A chosen device needs the plain HAL unit. If it has gone or cannot be
  // selected, fall back to the system default.
  AudioDeviceID chosen = deviceUID.empty() ? 0 : deviceForUID(deviceUID);
  AudioUnit unit = chosen ? newOutputUnit(chosen) : nullptr;
  if (!unit) {
    chosen = 0;
    unit = newOutputUnit(0);
  }
  if (!unit) return false;

  // Ask for the device's own rate so Core Audio does not resample.
  AudioStreamBasicDescription deviceFormat{};
  UInt32 size = sizeof(deviceFormat);
  AudioUnitGetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0,
                       &deviceFormat, &size);
  double rate = deviceFormat.mSampleRate > 0 ? deviceFormat.mSampleRate : 48000.0;

  AudioStreamBasicDescription fmt{};
  fmt.mSampleRate = rate;
  fmt.mFormatID = kAudioFormatLinearPCM;
  fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagIsPacked;
  fmt.mBitsPerChannel = 32;
  fmt.mChannelsPerFrame = 2;
  fmt.mFramesPerPacket = 1;
  fmt.mBytesPerFrame = 4;
  fmt.mBytesPerPacket = 4;
  if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt,
                           sizeof(fmt)) != noErr) {
    AudioComponentInstanceDispose(unit);
    return false;
  }

  AURenderCallbackStruct cb{renderCallback, this};
  AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb,
                       sizeof(cb));
  sampleRate_ = rate;
  if (prepare_) prepare_(rate);
  if (AudioUnitInitialize(unit) != noErr) {
    AudioComponentInstanceDispose(unit);
    return false;
  }
  // The size is set after initialising, or the unit resets it.
  requestSmallBuffer(unit);
  startedAt_ = mach_absolute_time();
  started_ = true;
  if (AudioOutputUnitStart(unit) != noErr) {
    started_ = false;
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
    return false;
  }
  {
    std::lock_guard<std::mutex> g(mu_);
    unit_ = unit;
    watched_ = chosen;
  }
  AudioUnitAddPropertyListener(unit, kAudioOutputUnitProperty_CurrentDevice, AudioListeners::unitDevice, this);
  if (chosen)
    AudioObjectAddPropertyListener(chosen, &kAliveAddress, AudioListeners::alive, this);
  else
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &kDefaultAddress, AudioListeners::defaultChanged,
                                   this);
  return true;
}

void AudioOutput::deviceChanged() {
  std::lock_guard<std::mutex> g(mu_);
  if (unit_) requestSmallBuffer(static_cast<AudioUnit>(unit_));
}

void AudioOutput::checkAlive() {
  {
    std::lock_guard<std::mutex> g(mu_);
    if (!unit_ || !watched_) return;
    UInt32 alive = 0, size = sizeof(alive);
    if (AudioObjectGetPropertyData(watched_, &kAliveAddress, 0, nullptr, &size, &alive) == noErr && alive) return;
  }
  if (lost_) lost_();
}

bool AudioOutput::running() const {
  if (!started_.load()) return false;
  uint64_t last = std::max(startedAt_.load(), lastRender_.load(std::memory_order_relaxed));
  return mach_absolute_time() - last < nanosToTicks(250000000ull);
}

AudioOutput::Latency AudioOutput::latency() const {
  Latency l;
  if (!unit_) return l;
  AudioDeviceID device = 0;
  UInt32 size = sizeof(device);
  if (AudioUnitGetProperty(static_cast<AudioUnit>(unit_), kAudioOutputUnitProperty_CurrentDevice,
                           kAudioUnitScope_Global, 0, &device, &size) != noErr || !device)
    return l;
  auto get = [&](AudioObjectPropertySelector sel, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress a{sel, scope, kAudioObjectPropertyElementMain};
    UInt32 v = 0, sz = sizeof(v);
    AudioObjectGetPropertyData(device, &a, 0, nullptr, &sz, &v);
    return v;
  };
  l.bufferFrames = get(kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal);
  l.deviceFrames = get(kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput);
  l.safetyFrames = get(kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput);
  AudioObjectPropertyAddress sa{kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                                kAudioObjectPropertyElementMain};
  UInt32 ssz = 0;
  if (AudioObjectGetPropertyDataSize(device, &sa, 0, nullptr, &ssz) == noErr && ssz >= sizeof(AudioStreamID)) {
    AudioStreamID streams[8];
    ssz = std::min<UInt32>(ssz, sizeof(streams));
    if (AudioObjectGetPropertyData(device, &sa, 0, nullptr, &ssz, streams) == noErr) {
      AudioObjectPropertyAddress la{kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
      UInt32 v = 0, sz = sizeof(v);
      AudioObjectGetPropertyData(streams[0], &la, 0, nullptr, &sz, &v);
      l.streamFrames = v;
    }
  }
  return l;
}

void AudioOutput::stop() {
  started_ = false;
  AudioDeviceID watched;
  {
    std::lock_guard<std::mutex> g(mu_);
    if (!unit_) return;
    watched = watched_;
  }
  if (watched)
    AudioObjectRemovePropertyListener(watched, &kAliveAddress, AudioListeners::alive, this);
  else
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &kDefaultAddress, AudioListeners::defaultChanged,
                                      this);
  std::lock_guard<std::mutex> g(mu_);
  auto unit = static_cast<AudioUnit>(unit_);
  AudioOutputUnitStop(unit);
  AudioUnitUninitialize(unit);
  AudioComponentInstanceDispose(unit);
  unit_ = nullptr;
  watched_ = 0;
}

void AudioOutput::render(float* left, float* right, uint32_t frames) {
  lastRender_.store(mach_absolute_time(), std::memory_order_relaxed);
  fn_(left, right, frames);
}

}  // namespace mf1
