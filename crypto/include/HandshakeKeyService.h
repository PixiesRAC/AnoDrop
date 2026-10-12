#pragma once
#include <array>
#include <expected>
#include <string>
#include <vector>

#include <openssl/crypto.h>

class HandshakeKeyService
{
  public:
    struct ChannelKeys
    {
        std::array<char, 32> publicKey;
        std::array<char, 64> privateKey;
        std::string pubKeyHex;
        std::string targetIDHex;
        ChannelKeys(const std::vector<unsigned char> &seed, const std::string &salt = "");
        ~ChannelKeys()
        {
            OPENSSL_cleanse(privateKey.data(), privateKey.size());
        }
        ChannelKeys(const ChannelKeys &) = default;
        ChannelKeys(ChannelKeys &&) noexcept = default;
        ChannelKeys &operator=(const ChannelKeys &) = default;
        ChannelKeys &operator=(ChannelKeys &&) noexcept = default;
    };

    [[nodiscard]] static std::expected<std::string, std::string> deriveSharedSecret(const std::string &myPrivHex, const std::string &peerPubHex, const std::string &lobbyName);
};
