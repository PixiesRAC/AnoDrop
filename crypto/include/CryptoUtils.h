#pragma once
#include <expected>
#include <nlohmann/json.hpp>

namespace CryptoUtils
{
std::expected<std::vector<unsigned char>, std::string> encryptAES256GCM(const std::vector<unsigned char> &plaintext, const std::vector<unsigned char> &key, const std::vector<unsigned char>& aad = {});

std::expected<std::vector<unsigned char>, std::string> decryptAES256GCM(const std::vector<unsigned char> &ciphertext_blob, const std::vector<unsigned char> &key, const std::vector<unsigned char>& aad = {});

std::vector<unsigned char> deriveStoreKey(const std::string &password, const std::vector<unsigned char> &salt);

std::expected<std::vector<unsigned char>, std::string> deriveHKDF(const std::vector<unsigned char>& ikm, const std::vector<unsigned char>& salt, const std::string& info, size_t outLen = 32);

struct X25519KeyPair
{
    std::vector<unsigned char> publicKey;
    std::vector<unsigned char> privateKey;
};
std::expected<X25519KeyPair, std::string> generateX25519KeyPair();

std::expected<std::vector<unsigned char>, std::string> deriveX25519SharedSecret(const std::vector<unsigned char> &myPrivateKey, const std::vector<unsigned char> &peerPublicKey);
} // namespace CryptoUtils
