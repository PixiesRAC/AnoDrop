#pragma once

#include <expected>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "Bep44Signer.h"
#include "CryptoUtils.h"
#include "LobbyData.h"
#include "LobbyDataSerializer.h"
#include "SecureBuffer.h"
#include "TimeUtils.h"
#include "Util.h"
#include <spdlog/spdlog.h>

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

    // Chat payloads travel sealed on the DHT, so the size budget includes the sealed-box overhead.
    [[nodiscard]] static constexpr bool fitsBep44Sealed(std::size_t plaintextSize) noexcept
    {
        const std::size_t sealedSize = plaintextSize + CryptoUtils::SEALED_BOX_OVERHEAD;
        return sealedSize <= 1000 && Bep44Signer::bencodedStringSize(sealedSize) <= 1000;
    }

    // Binds the ciphertext to its DHT slot: a sealed body copied to another sequence fails authentication.
    [[nodiscard]] static std::string chatMessageAad(std::int64_t seqNum)
    {
        return "AnoDropMessage/v1/" + std::to_string(seqNum);
    }

    // Seals the plaintext ChatMessage JSON for the peer, right before the DHT PUT.
    // Only the holder of the peer's X25519 private key can open the result (the sender cannot).
    [[nodiscard]] static std::expected<std::string, std::string> sealChatPayload(std::string_view plaintextJson, std::string_view recipientX25519PubHex, std::int64_t seqNum)
    {
        auto recipientPub = Util::fromHex(recipientX25519PubHex);
        if (recipientPub.size() != 32)
            return std::unexpected("Invalid recipient X25519 public key");
        const auto aad = chatMessageAad(seqNum);
        auto sealed = CryptoUtils::sealX25519(
            std::span(reinterpret_cast<const unsigned char *>(plaintextJson.data()), plaintextJson.size()), recipientPub,
            std::span(reinterpret_cast<const unsigned char *>(aad.data()), aad.size()));
        if (!sealed)
            return std::unexpected(sealed.error());
        std::string ephemPubHex = Util::toHex(std::span<const unsigned char>(sealed->data(), 32));
        SPDLOG_INFO("[CRYPTO] ECIES sealed box created | seq={} recipient_pub={} ephemeral_pub={} (ephemeral private key wiped)",
                    seqNum, recipientX25519PubHex.substr(0, std::min<size_t>(16, recipientX25519PubHex.size())),
                    ephemPubHex.substr(0, std::min<size_t>(16, ephemPubHex.size())));
        return std::string(sealed->begin(), sealed->end());
    }

    // Opens a sealed DHT chat payload with our X25519 private key, then parses the plaintext JSON.
    [[nodiscard]] static std::expected<ChatMessage, std::string> openChatMessage(const std::string &sealedPayload, std::string_view myX25519PrivHex, std::int64_t seqNumTarget)
    {
        auto myPriv = Util::fromHex(myX25519PrivHex);
        ScopedCleanse privGuard(myPriv);
        if (myPriv.size() != 32)
            return std::unexpected("Invalid local X25519 private key");
        const auto aad = chatMessageAad(seqNumTarget);
        auto opened = CryptoUtils::openX25519(
            std::span(reinterpret_cast<const unsigned char *>(sealedPayload.data()), sealedPayload.size()), myPriv,
            std::span(reinterpret_cast<const unsigned char *>(aad.data()), aad.size()));
        if (!opened)
            return std::unexpected("Failed to open sealed ChatMessage: " + opened.error());
        std::string ephemPubHex = sealedPayload.size() >= 32
            ? Util::toHex(std::span<const unsigned char>(reinterpret_cast<const unsigned char *>(sealedPayload.data()), 32))
            : "";
        SPDLOG_INFO("[CRYPTO] ECIES sealed box opened | seq={} ephemeral_pub={}",
                    seqNumTarget, ephemPubHex.substr(0, std::min<size_t>(16, ephemPubHex.size())));
        std::string plaintext(opened->begin(), opened->end());
        ScopedCleanse openedGuard(*opened);
        ScopedCleanse plaintextGuard(plaintext);
        return parseChatMessage(plaintext, seqNumTarget);
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
