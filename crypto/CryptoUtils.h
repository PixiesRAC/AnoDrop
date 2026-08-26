#pragma once
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <expected>

namespace CryptoUtils
{
    std::expected<std::vector<unsigned char>, std::string> encryptAES256GCM(const std::vector<unsigned char> &plaintext, const std::vector<unsigned char> &key);

    std::expected<std::vector<unsigned char>, std::string> decryptAES256GCM(const std::vector<unsigned char> &ciphertext_blob, const std::vector<unsigned char> &key);

    struct X25519KeyPair
    {
        std::vector<unsigned char> publicKey;
        std::vector<unsigned char> privateKey;
    };
    std::expected<X25519KeyPair, std::string> generateX25519KeyPair();

    std::expected<std::vector<unsigned char>, std::string> deriveX25519SharedSecret(const std::vector<unsigned char> &myPrivateKey, const std::vector<unsigned char> &peerPublicKey);
} // namespace CryptoUtils
