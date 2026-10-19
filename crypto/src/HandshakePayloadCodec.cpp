#include "HandshakePayloadCodec.h"

#include <nlohmann/json.hpp>
#include <span>

#include "CryptoUtils.h"
#include "SecureBuffer.h"

std::expected<nlohmann::json, std::string> HandshakePayloadCodec::decryptDhtHandShakePayload(const std::string &ciphertextStr, const std::string &lobbyName)
{
    try
    {
        std::span<const unsigned char> ciphertext(
            reinterpret_cast<const unsigned char *>(ciphertextStr.data()),
            ciphertextStr.size());
        std::span<const unsigned char> lobbySpan(reinterpret_cast<const unsigned char *>(lobbyName.data()), lobbyName.size());
        auto lobbyKeyRes = CryptoUtils::deriveHKDF(lobbySpan, {}, "HandshakeKey", 32);
        if (!lobbyKeyRes)
            return std::unexpected(std::move(lobbyKeyRes).error());
        auto lobbyKey = std::move(*lobbyKeyRes);
        ScopedCleanse keyGuard(lobbyKey);

        auto plaintextRes = CryptoUtils::decryptAES256GCM(ciphertext, lobbyKey);
        if (!plaintextRes)
        {
            return std::unexpected(std::move(plaintextRes).error());
        }
        auto plaintext = std::move(*plaintextRes);
        ScopedCleanse plainGuard(plaintext);

        return nlohmann::json::parse(plaintext.begin(), plaintext.end());
    }
    catch (const std::exception &e)
    {
        return std::unexpected(e.what());
    }
}

std::expected<std::vector<unsigned char>, std::string> HandshakePayloadCodec::encryptDhtHandshakePayload(const nlohmann::json &payload, const std::string &lobbyName)
{
    std::string jsonStr = payload.dump();
    ScopedCleanse jsonGuard(jsonStr);
    std::span<const unsigned char> plaintextBytes(
        reinterpret_cast<const unsigned char *>(jsonStr.data()),
        jsonStr.size());

    std::span<const unsigned char> lobbySpan(reinterpret_cast<const unsigned char *>(lobbyName.data()), lobbyName.size());
    auto lobbyKeyRes = CryptoUtils::deriveHKDF(lobbySpan, {}, "HandshakeKey", 32);
    if (!lobbyKeyRes)
        return std::unexpected(std::move(lobbyKeyRes).error());
    auto lobbyKey = std::move(*lobbyKeyRes);
    ScopedCleanse keyGuard(lobbyKey);

    auto encryptedRes = CryptoUtils::encryptAES256GCM(plaintextBytes, lobbyKey);

    if (!encryptedRes)
    {
        return std::unexpected(std::move(encryptedRes).error());
    }

    return std::move(*encryptedRes);
}
