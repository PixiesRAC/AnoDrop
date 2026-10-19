#pragma once

#include <array>
#include <libtorrent/hasher.hpp>
#include <span>
#include <string>

#include "Util.h"

namespace network::logging
{
inline std::string sha1(const std::string &bytes)
{
    lt::hasher hash(bytes.data(), static_cast<int>(bytes.size()));
    const auto digest = hash.final();
    return Util::toHex(std::span<const char>(digest.data(), digest.size()));
}
inline std::string target(const std::array<char, 32> &key, const std::string &salt)
{
    return sha1(std::string(key.begin(), key.end()) + salt);
}
} // namespace network::logging
