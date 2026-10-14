#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mf1 {

// DX7 checksum: the two's complement of the 7 bit sum of the data.
uint8_t dx7Checksum(const uint8_t* data, size_t len);

// Builds F0 43 00 09 20 00 <4096> ck F7 from 32 packed VMEM voices.
std::vector<uint8_t> makeBankDump(const uint8_t vmem[32 * 128]);
// Builds F0 43 00 00 01 1B <155> ck F7 from a VCED voice.
std::vector<uint8_t> makeVoiceDump(const uint8_t vced[155]);

// M-VAVE vendor packets: 00 59 cmd len:u24le payload ~sum(payload), sent
// packed 8 to 7 bits as one LSB-first bitstream between F0 and F7.
std::vector<uint8_t> pack7lsb(const std::vector<uint8_t>& in);
std::vector<uint8_t> unpack7lsb(const uint8_t* in, size_t len);
std::vector<uint8_t> makeVendorMessage(uint8_t cmd, const std::vector<uint8_t>& payload);

struct VendorPacket {
  uint8_t cmd = 0;
  std::vector<uint8_t> payload;
};
// Decodes a whole F0..F7 message; false when it is not a valid packet.
bool parseVendorMessage(const uint8_t* msg, size_t len, VendorPacket& out);

}  // namespace mf1
