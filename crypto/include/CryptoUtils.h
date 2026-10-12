#pragma once
#include <expected>
#include <nlohmann/json.hpp>

#include <openssl/crypto.h>

namespace CryptoUtils
{
[[nodiscard]] std::expected<std::vector<unsigned char>, std::string> encryptAES256GCM(const std::vector<unsigned char> &plaintext, const std::vector<unsigned char> &key, const std::vector<unsigned char>& aad = {});

[[nodiscard]] std::expected<std::vector<unsigned char>, std::string> decryptAES256GCM(const std::vector<unsigned char> &ciphertext_blob, const std::vector<unsigned char> &key, const std::vector<unsigned char>& aad = {});

[[nodiscard]] std::vector<unsigned char> deriveStoreKey(const std::string &password, const std::vector<unsigned char> &salt);

[[nodiscard]] std::expected<std::vector<unsigned char>, std::string> deriveHKDF(const std::vector<unsigned char>& ikm, const std::vector<unsigned char>& salt, const std::string& info, size_t outLen = 32);

struct X25519KeyPair
{
    std::vector<unsigned char> publicKey;
    std::vector<unsigned char> privateKey;

    X25519KeyPair() = default;
    X25519KeyPair(std::vector<unsigned char> pub, std::vector<unsigned char> priv)
        : publicKey(std::move(pub)), privateKey(std::move(priv)) {}
    ~X25519KeyPair()
    {
        if (!privateKey.empty())
        {
            OPENSSL_cleanse(privateKey.data(), privateKey.size());
        }
    }
    X25519KeyPair(const X25519KeyPair &other)
        : publicKey(other.publicKey), privateKey(other.privateKey) {}
    X25519KeyPair(X25519KeyPair &&other) noexcept
        : publicKey(std::move(other.publicKey)), privateKey(std::move(other.privateKey)) {}
    X25519KeyPair &operator=(const X25519KeyPair &other)
    {
        if (this != &other)
        {
            if (!privateKey.empty())
                OPENSSL_cleanse(privateKey.data(), privateKey.size());
            publicKey = other.publicKey;
            privateKey = other.privateKey;
        }
        return *this;
    }
    X25519KeyPair &operator=(X25519KeyPair &&other) noexcept
    {
        if (this != &other)
        {
            if (!privateKey.empty())
                OPENSSL_cleanse(privateKey.data(), privateKey.size());
            publicKey = std::move(other.publicKey);
            privateKey = std::move(other.privateKey);
        }
        return *this;
    }
};
[[nodiscard]] std::expected<X25519KeyPair, std::string> generateX25519KeyPair();

[[nodiscard]] std::expected<std::vector<unsigned char>, std::string> deriveX25519SharedSecret(const std::vector<unsigned char> &myPrivateKey, const std::vector<unsigned char> &peerPublicKey);
} // namespace CryptoUtils
