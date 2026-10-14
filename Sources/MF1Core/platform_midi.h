#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mf1 {

// CoreMIDI client. Publishes a virtual destination and source under the
// given name, so librarians, editors and DAWs can talk to the app as if it
// were the hardware, and listens to every hardware source too.
class MidiIO {
 public:
  // Each call carries one complete message: a channel or realtime message,
  // or a whole SysEx from F0 to F7. Runs on a CoreMIDI thread.
  using Handler = std::function<void(const uint8_t* data, size_t len)>;
  ~MidiIO();
  bool start(const std::string& portName, Handler handler);
  // Sends on the virtual source only. Hardware outputs are left alone so a
  // dump can never overwrite a connected synth by accident.
  void send(const uint8_t* data, size_t len);
  void rescanSources();

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

// Encodes one message into event lists of at most maxListBytes (0 for the
// size sending uses) and decodes them again with the input parser. For tests.
std::vector<std::vector<uint8_t>> midiLoopback(const uint8_t* data, size_t len, size_t maxListBytes,
                                               int* lists);

}  // namespace mf1
