#include "sysex.h"

namespace mf1 {

uint8_t dx7Checksum(const uint8_t* data, size_t len) {
  unsigned sum = 0;
  for (size_t i = 0; i < len; i++) sum += data[i];
  return (uint8_t)((128 - (sum & 0x7f)) & 0x7f);
}

std::vector<uint8_t> makeBankDump(const uint8_t vmem[32 * 128]) {
  std::vector<uint8_t> out = {0xf0, 0x43, 0x00, 0x09, 0x20, 0x00};
  out.insert(out.end(), vmem, vmem + 4096);
  out.push_back(dx7Checksum(vmem, 4096));
  out.push_back(0xf7);
  return out;
}

std::vector<uint8_t> makeVoiceDump(const uint8_t vced[155]) {
  std::vector<uint8_t> out = {0xf0, 0x43, 0x00, 0x00, 0x01, 0x1b};
  out.insert(out.end(), vced, vced + 155);
  out.push_back(dx7Checksum(vced, 155));
  out.push_back(0xf7);
  return out;
}

std::vector<uint8_t> pack7lsb(const std::vector<uint8_t>& in) {
  std::vector<uint8_t> out;
  uint32_t buf = 0;
  int n = 0;
  for (uint8_t v : in) {
    buf |= (uint32_t)v << n;
    n += 8;
    while (n >= 7) {
      out.push_back(buf & 0x7f);
      buf >>= 7;
      n -= 7;
    }
  }
  if (n) out.push_back(buf & 0x7f);
  return out;
}

std::vector<uint8_t> unpack7lsb(const uint8_t* in, size_t len) {
  std::vector<uint8_t> out;
  uint32_t buf = 0;
  int n = 0;
  for (size_t i = 0; i < len; i++) {
    buf |= (uint32_t)(in[i] & 0x7f) << n;
    n += 7;
    while (n >= 8) {
      out.push_back(buf & 0xff);
      buf >>= 8;
      n -= 8;
    }
  }
  return out;
}

std::vector<uint8_t> makeVendorMessage(uint8_t cmd, const std::vector<uint8_t>& payload) {
  size_t lp = payload.size();
  std::vector<uint8_t> p = {0x00, 0x59, cmd, (uint8_t)(lp & 0xff), (uint8_t)((lp >> 8) & 0xff),
                            (uint8_t)((lp >> 16) & 0xff)};
  unsigned sum = 0;
  for (uint8_t b : payload) sum += b;
  p.insert(p.end(), payload.begin(), payload.end());
  p.push_back((uint8_t)((sum & 0xff) ^ 0xff));
  std::vector<uint8_t> out = {0xf0};
  auto packed = pack7lsb(p);
  out.insert(out.end(), packed.begin(), packed.end());
  out.push_back(0xf7);
  return out;
}

bool parseVendorMessage(const uint8_t* msg, size_t len, VendorPacket& out) {
  if (len < 10 || msg[0] != 0xf0 || msg[len - 1] != 0xf7) return false;
  // 00 59 packs to 00 32 on the wire.
  if (msg[1] != 0x00 || msg[2] != 0x32) return false;
  auto p = unpack7lsb(msg + 1, len - 2);
  if (p.size() < 7 || p[0] != 0x00 || p[1] != 0x59) return false;
  size_t lp = p[3] | (p[4] << 8) | (p[5] << 16);
  // The last septet may carry a padding byte, so allow one extra.
  if (p.size() < lp + 7) return false;
  unsigned sum = 0;
  for (size_t i = 0; i < lp; i++) sum += p[6 + i];
  if (((sum & 0xff) ^ 0xff) != p[6 + lp]) return false;
  out.cmd = p[2];
  out.payload.assign(p.begin() + 6, p.begin() + 6 + lp);
  return true;
}

}  // namespace mf1
