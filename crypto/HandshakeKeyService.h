#pragma once
#include <array>
#include <string>
#include <vector>
#include <expected>

class HandshakeKeyService {
public:
    struct ChannelKeys {
        std::array<char, 32> publicKey;
        std::array<char, 64> privateKey;
        std::string pubKeyHex;
        std::string targetIDHex;
        ChannelKeys(const std::vector<unsigned char> &seed, const std::string &salt = "");
    };

    static std::expected<std::string, std::string> deriveSharedSecret(const std::string &myPrivHex, const std::string &peerPubHex, const std::string &lobbyName);
};
