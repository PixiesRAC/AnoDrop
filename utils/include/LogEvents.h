#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace LogEvents {
inline std::string lobbyId(std::string_view name)
{
    return nlohmann::json(name).dump();
}
} // namespace LogEvents
