#pragma once
#include <expected>
#include <nlohmann/json.hpp>

class HandshakePayloadCodec
{
  public:
    [[nodiscard]] static std::expected<nlohmann::json, std::string> decryptDhtHandShakePayload(const std::string &ciphertextStr, const std::string &lobbyName);
    [[nodiscard]] static std::expected<std::vector<unsigned char>, std::string> encryptDhtHandshakePayload(const nlohmann::json &payload, const std::string &lobbyName);
};
