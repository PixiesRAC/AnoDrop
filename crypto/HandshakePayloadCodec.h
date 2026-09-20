#pragma once
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <expected>
#include <optional>

class HandshakePayloadCodec {
public:
    static std::expected<nlohmann::json, std::string> decryptDhtHandShakePayload(const std::string &ciphertextStr, const std::string &lobbyName);
    static std::expected<std::vector<unsigned char>, std::string> encryptDhtHandshakePayload(const nlohmann::json &payload, const std::string &lobbyName);
};
