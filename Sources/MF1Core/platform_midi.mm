#include "platform_midi.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace mf1 {

namespace {

// Reassembles MIDI 1.0 byte messages from Universal MIDI Packets.
struct UmpParser {
  // Longer SysEx messages are dropped; the largest real one is under 20 KB.
  static constexpr size_t kMaxSysex = 64 * 1024;
  std::vector<uint8_t> sysex;
  bool inSysex = false;

  template <typename Emit>
  void parse(const MIDIEventList* list, Emit&& emit) {
    const MIDIEventPacket* pkt = &list->packet[0];
    for (UInt32 p = 0; p < list->numPackets; p++) {
      UInt32 i = 0;
      while (i < pkt->wordCount) {
        uint32_t w0 = pkt->words[i];
        int mt = (int)(w0 >> 28);
        int words = mt <= 2 ? 1 : (mt <= 4 ? 2 : (mt == 5 ? 4 : 1));
        if (mt == 1) {
          uint8_t status = (w0 >> 16) & 0xff;
          uint8_t msg[3] = {status, (uint8_t)((w0 >> 8) & 0x7f), (uint8_t)(w0 & 0x7f)};
          size_t len = status == 0xf2 ? 3 : (status == 0xf1 || status == 0xf3) ? 2 : 1;
          emit(msg, len);
        } else if (mt == 2) {
          uint8_t status = (w0 >> 16) & 0xff;
          uint8_t msg[3] = {status, (uint8_t)((w0 >> 8) & 0x7f), (uint8_t)(w0 & 0x7f)};
          uint8_t type = status & 0xf0;
          emit(msg, (type == 0xc0 || type == 0xd0) ? 2 : 3);
        } else if (mt == 3 && i + 1 < pkt->wordCount) {
          uint32_t w1 = pkt->words[i + 1];
          int st = (w0 >> 20) & 0xf;
          int count = (w0 >> 16) & 0xf;
          uint8_t bytes[6] = {(uint8_t)((w0 >> 8) & 0x7f), (uint8_t)(w0 & 0x7f),
                              (uint8_t)((w1 >> 24) & 0x7f), (uint8_t)((w1 >> 16) & 0x7f),
                              (uint8_t)((w1 >> 8) & 0x7f), (uint8_t)(w1 & 0x7f)};
          if (st == 0 || st == 1) {
            sysex.clear();
            sysex.push_back(0xf0);
            inSysex = true;
          }
          if (inSysex) {
            for (int b = 0; b < count && b < 6; b++) sysex.push_back(bytes[b]);
            if (sysex.size() > kMaxSysex) {
              inSysex = false;
              sysex.clear();
              sysex.shrink_to_fit();
            } else if (st == 0 || st == 3) {
              sysex.push_back(0xf7);
              emit(sysex.data(), sysex.size());
              sysex.clear();
              inSysex = false;
            }
          }
        }
        i += words;
      }
      pkt = MIDIEventPacketNext(pkt);
    }
  }
};

// Encodes one message as MIDI 1.0 UMP event lists of at most maxBytes
// each, handing every full list to send. A long SysEx spans several lists.
template <typename Send>
void encodeEventLists(const uint8_t* data, size_t len, size_t maxBytes, Send&& send) {
  if (len == 0) return;
  size_t packets = data[0] == 0xf0 ? len / 6 + 2 : 1;
  // Each SysEx7 event may take a packet of its own.
  size_t bytes = std::min(maxBytes, sizeof(MIDIEventList) + packets * sizeof(MIDIEventPacket));
  std::vector<uint8_t> storage(bytes);
  auto* list = reinterpret_cast<MIDIEventList*>(storage.data());
  MIDIEventPacket* pkt = MIDIEventListInit(list, kMIDIProtocol_1_0);
  auto add = [&](UInt32 count, const UInt32* words) {
    MIDIEventPacket* next = MIDIEventListAdd(list, storage.size(), pkt, 0, count, words);
    if (!next && list->numPackets) {
      send(list);
      pkt = MIDIEventListInit(list, kMIDIProtocol_1_0);
      next = MIDIEventListAdd(list, storage.size(), pkt, 0, count, words);
    }
    if (next) pkt = next;
    return next != nullptr;
  };
  if (data[0] == 0xf0) {
    const uint8_t* body = data + 1;
    size_t n = len - 1;
    if (n && body[n - 1] == 0xf7) n--;
    size_t off = 0;
    bool first = true;
    do {
      size_t chunk = std::min<size_t>(6, n - off);
      bool last = off + chunk >= n;
      uint32_t st = first && last ? 0 : first ? 1 : last ? 3 : 2;
      uint8_t b[6] = {};
      for (size_t i = 0; i < chunk; i++) b[i] = body[off + i];
      UInt32 words[2] = {(3u << 28) | (st << 20) | ((uint32_t)chunk << 16) | ((uint32_t)b[0] << 8) | b[1],
                         ((uint32_t)b[2] << 24) | ((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 8) | b[5]};
      if (!add(2, words)) return;
      off += chunk;
      first = false;
    } while (off < n);
  } else {
    uint8_t status = data[0];
    uint32_t mt = status >= 0xf0 ? 1 : 2;
    UInt32 word = (mt << 28) | ((uint32_t)status << 16) | ((len > 1 ? (uint32_t)data[1] : 0) << 8) |
                  (len > 2 ? data[2] : 0);
    if (!add(1, &word)) return;
  }
  if (list->numPackets) send(list);
}

// The most CoreMIDI is handed in one call.
constexpr size_t kMaxListBytes = 64 * 1024;

CFStringRef cfstr(const std::string& s) {
  return CFStringCreateWithCString(nullptr, s.c_str(), kCFStringEncodingUTF8);
}

}  // namespace

struct MidiIO::Impl {
  MIDIClientRef client = 0;
  MIDIPortRef inPort = 0;
  MIDIEndpointRef virtualDest = 0;
  MIDIEndpointRef virtualSource = 0;
  Handler handler;
  std::mutex lock;
  // One parser per input, so interleaved SysEx streams do not mix.
  std::map<MIDIEndpointRef, std::unique_ptr<UmpParser>> parsers;
  // Parsers of removed sources live until the next setup change, in case
  // a callback for that source is still running.
  std::vector<std::unique_ptr<UmpParser>> retired;
  UmpParser virtualParser;

  // Another MF-1 window's port sends what that window plays. Listening to it would play one window's notes in
  // every other window, so those ports are left out; a real FM-1, keyboards and DAWs are still heard.
  static bool isMF1Window(MIDIEndpointRef src) {
    CFStringRef name = nullptr;
    if (MIDIObjectGetStringProperty(src, kMIDIPropertyName, &name) != noErr || !name) return false;
    bool ours = CFStringHasSuffix(name, CFSTR(" (FM-1 Compatible)"));
    CFRelease(name);
    return ours;
  }

  void connectAll() {
    std::lock_guard<std::mutex> g(lock);
    retired.clear();
    std::set<MIDIEndpointRef> present;
    ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; i++) {
      MIDIEndpointRef src = MIDIGetSource(i);
      present.insert(src);
      if (src == virtualSource || parsers.count(src) || isMF1Window(src)) continue;
      auto parser = std::make_unique<UmpParser>();
      if (MIDIPortConnectSource(inPort, src, parser.get()) == noErr) parsers[src] = std::move(parser);
    }
    for (auto it = parsers.begin(); it != parsers.end();) {
      if (present.count(it->first)) {
        ++it;
        continue;
      }
      MIDIPortDisconnectSource(inPort, it->first);
      retired.push_back(std::move(it->second));
      it = parsers.erase(it);
    }
  }
};

MidiIO::~MidiIO() {
  if (!impl_) return;
  if (impl_->client) MIDIClientDispose(impl_->client);
  delete impl_;
}

bool MidiIO::start(const std::string& portName, Handler handler) {
  impl_ = new Impl;
  impl_->handler = std::move(handler);
  Impl* impl = impl_;
  CFStringRef name = cfstr(portName);
  OSStatus err = MIDIClientCreateWithBlock(name, &impl->client, ^(const MIDINotification* n) {
    if (n->messageID == kMIDIMsgSetupChanged) impl->connectAll();
  });
  if (err != noErr) {
    CFRelease(name);
    return false;
  }
  // The virtual ports carry the product name so web librarians and
  // editors list them like the hardware.
  MIDIDestinationCreateWithProtocol(impl->client, name, kMIDIProtocol_1_0, &impl->virtualDest,
                                    ^(const MIDIEventList* list, void*) {
                                      impl->virtualParser.parse(list, [&](const uint8_t* d, size_t l) {
                                        impl->handler(d, l);
                                      });
                                    });
  MIDISourceCreateWithProtocol(impl->client, name, kMIDIProtocol_1_0, &impl->virtualSource);
  CFStringRef inName = cfstr(portName + " In");
  MIDIInputPortCreateWithProtocol(impl->client, inName, kMIDIProtocol_1_0, &impl->inPort,
                                  ^(const MIDIEventList* list, void* refCon) {
                                    auto* parser = static_cast<UmpParser*>(refCon);
                                    if (!parser) return;
                                    parser->parse(list, [&](const uint8_t* d, size_t l) {
                                      impl->handler(d, l);
                                    });
                                  });
  CFRelease(inName);
  CFRelease(name);
  impl->connectAll();
  return true;
}

void MidiIO::rescanSources() {
  if (impl_) impl_->connectAll();
}

void MidiIO::send(const uint8_t* data, size_t len) {
  if (!impl_ || len == 0) return;
  encodeEventLists(data, len, kMaxListBytes,
                   [&](const MIDIEventList* list) { MIDIReceivedEventList(impl_->virtualSource, list); });
}

std::vector<std::vector<uint8_t>> midiLoopback(const uint8_t* data, size_t len, size_t maxListBytes,
                                               int* lists) {
  std::vector<std::vector<uint8_t>> out;
  UmpParser parser;
  int count = 0;
  encodeEventLists(data, len, maxListBytes ? maxListBytes : kMaxListBytes, [&](const MIDIEventList* list) {
    count++;
    parser.parse(list, [&](const uint8_t* d, size_t l) { out.emplace_back(d, d + l); });
  });
  if (lists) *lists = count;
  return out;
}

}  // namespace mf1
