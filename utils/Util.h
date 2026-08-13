#pragma once

#include <array>
#include <cstddef>
#include <string>

enum class IpProtocol {
    IPv4,
    IPv6
};

namespace Util {

// Helper to convert array of chars to hex
template <size_t N> static std::string toHex(const std::array<char, N> &bytes) {
  std::string hex;
  const char hexChars[] = "0123456789abcdef";
  for (char b : bytes) {
    unsigned char ub = static_cast<unsigned char>(b);
    hex += hexChars[(ub >> 4) & 0x0F];
    hex += hexChars[ub & 0x0F];
  }
  return hex;
}

static std::string toHex(const std::string &bytes) {
  std::string hex;
  const char hexChars[] = "0123456789abcdef";
  for (char b : bytes) {
    unsigned char ub = static_cast<unsigned char>(b);
    hex += hexChars[(ub >> 4) & 0x0F];
    hex += hexChars[ub & 0x0F];
  }
  return hex;
}

}