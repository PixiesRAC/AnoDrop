#pragma once
#include <array>
#include <expected>
#include <span>
#include <string>

#include <openssl/crypto.h>

class HandshakeKeyService
{
  public:
    struct ChannelKeys
    {
        std::array<char, 32> publicKey;
        std::array<char, 64> privateKey;
        std::string salt;
        std::string pubKeyHex;
        std::string targetIDHex;
        ChannelKeys(std::span<const unsigned char> seed, std::string saltValue = "");
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
