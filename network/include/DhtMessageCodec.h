#pragma once

#include <expected>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "Bep44Signer.h"
#include "LobbyData.h"
#include "LobbyDataSerializer.h"
#include "TimeUtils.h"

namespace network
{

/**
 * @brief Handles payload marshalling, unmarshalling, and timestamp validation for DHT items.
 * Decouples message serialization logic from network I/O orchestration in DhtPublisher.
 */
class DhtMessageCodec
{
  public:
    [[nodiscard]] static constexpr bool fitsBep44(std::string_view payload) noexcept
    {
        return payload.size() <= 1000 && Bep44Signer::bencodedStringSize(payload.size()) <= 1000;
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
