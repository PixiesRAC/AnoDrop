#pragma once

#include <cstddef>
#include <ranges>
#include <string>
// #include <vector>

enum class IpProtocol
{
    IPv4,
    IPv6
};

namespace Util
{

template <typename T>
concept ByteRange = std::ranges::input_range<T> && sizeof(std::ranges::range_value_t<T>) == 1;

template <ByteRange T>
inline std::string toHex(const T &bytes)
{
    std::string hex;
    const char hexChars[] = "0123456789abcdef";
    for (auto b : bytes)
    {
        unsigned char ub = static_cast<unsigned char>(b);
        hex += hexChars[(ub >> 4) & 0x0F];
        hex += hexChars[ub & 0x0F];
    }
    return hex;
}

inline std::vector<unsigned char> fromHex(const std::string &hex)
{
    std::vector<unsigned char> bytes;
    if (hex.length() % 2 != 0)
    {
        return bytes;
    }
    for (size_t i = 0; i < hex.length(); i += 2)
    {
        std::string byteString = hex.substr(i, 2);
        unsigned char byte = static_cast<unsigned char>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

} // namespace Util