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
[[nodiscard]] inline std::string toHex(const T &bytes)
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

[[nodiscard]] inline std::vector<unsigned char> fromHex(const std::string &hex)
{
    std::vector<unsigned char> bytes;
    if (hex.length() % 2 != 0)
    {
        return bytes;
    }
    bytes.reserve(hex.length() / 2);
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.length(); i += 2)
    {
        int hi = hexVal(hex[i]);
        int lo = hexVal(hex[i + 1]);
        if (hi < 0 || lo < 0)
        {
            bytes.clear();
            return bytes;
        }
        bytes.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return bytes;
}

} // namespace Util