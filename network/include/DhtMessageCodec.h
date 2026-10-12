#pragma once

#include <expected>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "CryptoUtils.h"
#include "LobbyData.h"
#include "TimeUtils.h"

namespace network
{

/**
 * @brief Handles payload marshalling, unmarshalling, seed derivation, and timestamp validation for DHT items.
 * Decouples message serialization logic from network I/O orchestration in DhtPublisher.
 */
class DhtMessageCodec
{
  public:
    [[nodiscard]] static std::vector<unsigned char> deriveSeed(const std::string &key, const std::string &info = "DHTSeed")
    {
        return CryptoUtils::deriveHKDF(
            std::vector<unsigned char>(key.begin(), key.end()),
            {},
            info,
            32).value_or(std::vector<unsigned char>{});
    }

    [[nodiscard]] static std::expected<ChatMessage, std::string> parseChatMessage(const std::string &valueStr, std::int64_t seqNumTarget)
    {
        try
        {
            nlohmann::json msgJson = nlohmann::json::parse(valueStr);
            ChatMessage chatMsg = msgJson.get<ChatMessage>();
            chatMsg.direction = MessageDirection::IN;
            chatMsg.seq_num = seqNumTarget;
            return chatMsg;
        }
        catch (const std::exception &e)
        {
            return std::unexpected(std::string("Failed to parse incoming ChatMessage JSON: ") + e.what());
        }
    }

    [[nodiscard]] static bool isTimestampValid(std::int64_t timestamp)
    {
        auto valResult = TimeUtils::validatePeerTimestamp(timestamp, 0, 0);
        return valResult != TimestampValidationResult::Corrupted;
    }
};

} // namespace network
