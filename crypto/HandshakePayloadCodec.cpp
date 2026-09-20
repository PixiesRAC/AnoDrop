#include "crypto/HandshakePayloadCodec.h"
#include "crypto/CryptoUtils.h"
#include "crypto/Hasher.h"
#include <spdlog/spdlog.h>
#include <span>

std::expected<nlohmann::json, std::string> HandshakePayloadCodec::decryptDhtHandShakePayload(const std::string &ciphertextStr, const std::string &lobbyName)
{
    try
    {
        std::vector<unsigned char> ciphertext(ciphertextStr.begin(), ciphertextStr.end());
        std::vector<unsigned char> lobbyKey = Hasher::getInstance().getHashMsg256(lobbyName);
        auto plaintextRes = CryptoUtils::decryptAES256GCM(ciphertext, lobbyKey);
        if (!plaintextRes)
        {
            return std::unexpected(plaintextRes.error());
        }
        std::vector<unsigned char> plaintext = *plaintextRes;
        std::string jsonStr(plaintext.begin(), plaintext.end());
        return nlohmann::json::parse(jsonStr);
    }
    catch (const std::exception &e)
    {
        return std::unexpected(e.what());
    }
}

std::expected<std::vector<unsigned char>, std::string> HandshakePayloadCodec::encryptDhtHandshakePayload(const nlohmann::json &payload, const std::string &lobbyName)
{
    std::string jsonStr = payload.dump();
    std::span<const unsigned char> plaintextBytes(
        reinterpret_cast<const unsigned char*>(jsonStr.data()), 
        jsonStr.size()
    );

    std::vector<unsigned char> lobbyKey = Hasher::getInstance().getHashMsg256(lobbyName);
    auto encryptedRes = CryptoUtils::encryptAES256GCM(
        std::vector<unsigned char>(plaintextBytes.begin(), plaintextBytes.end()), lobbyKey
    );

    if (!encryptedRes)
    {
        return std::unexpected(encryptedRes.error());
    }
    
    return *encryptedRes;
}
