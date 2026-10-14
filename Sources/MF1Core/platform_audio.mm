#include "platform_audio.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

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

bool AudioOutput::start(RenderFn fn) {
  fn_ = std::move(fn);
  AudioComponentDescription desc{};
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
  if (!comp) return false;
  AudioUnit unit = nullptr;
  if (AudioComponentInstanceNew(comp, &unit) != noErr) return false;

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

  // A small hardware buffer keeps key-to-sound latency low.
  AudioDeviceID device = 0;
  size = sizeof(device);
  if (AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                           &device, &size) == noErr && device) {
    UInt32 frames = 128;
    AudioObjectPropertyAddress addr{kAudioDevicePropertyBufferFrameSize,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    AudioObjectSetPropertyData(device, &addr, 0, nullptr, sizeof(frames), &frames);
  }

  AURenderCallbackStruct cb{renderCallback, this};
  AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb,
                       sizeof(cb));
  sampleRate_ = rate;
  if (prepare_) prepare_(rate);
  if (AudioUnitInitialize(unit) != noErr || AudioOutputUnitStart(unit) != noErr) {
    AudioComponentInstanceDispose(unit);
    return false;
  }
  unit_ = unit;
  return true;
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
