#include "platform_audio.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <algorithm>

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

bool AudioOutput::start(RenderFn fn, const std::string& deviceUID) {
  fn_ = std::move(fn);
  // A chosen device needs the plain HAL unit; if it has gone, fall back
  // to the system default.
  AudioDeviceID chosen = deviceUID.empty() ? 0 : deviceForUID(deviceUID);
  AudioComponentDescription desc{};
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = chosen ? kAudioUnitSubType_HALOutput : kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
  if (!comp) return false;
  AudioUnit unit = nullptr;
  if (AudioComponentInstanceNew(comp, &unit) != noErr) return false;
  if (chosen &&
      AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &chosen,
                           sizeof(chosen)) != noErr) {
    AudioComponentInstanceDispose(unit);
    return false;
  }

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
  // One 64 frame engine block per IO cycle. Notes start on block
  // boundaries, so a smaller buffer would not make them start sooner. The
  // size is set after initialising, or the unit resets it.
  AudioDeviceID device = 0;
  size = sizeof(device);
  AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device, &size);
  UInt32 frames = 64;
  AudioValueRange range{};
  AudioObjectPropertyAddress ra{kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal,
                                kAudioObjectPropertyElementMain};
  size = sizeof(range);
  if (device && AudioObjectGetPropertyData(device, &ra, 0, nullptr, &size, &range) == noErr)
    frames = (UInt32)std::max<Float64>(range.mMinimum, std::min<Float64>(range.mMaximum, frames));
  AudioUnitSetProperty(unit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0, &frames, sizeof(frames));
  if (AudioOutputUnitStart(unit) != noErr) {
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
    return false;
  }
  unit_ = unit;
  return true;
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
  if (!unit_) return;
  auto unit = static_cast<AudioUnit>(unit_);
  AudioOutputUnitStop(unit);
  AudioUnitUninitialize(unit);
  AudioComponentInstanceDispose(unit);
  unit_ = nullptr;
}

void AudioOutput::render(float* left, float* right, uint32_t frames) {
  fn_(left, right, frames);
}

}  // namespace mf1
