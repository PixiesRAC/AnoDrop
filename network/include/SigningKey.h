#pragma once
#include <array>
#include <openssl/crypto.h>
struct SigningKey {
    explicit SigningKey(const std::array<char, 64>& value) : bytes(value) {}
    ~SigningKey() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
    SigningKey(const SigningKey&) = delete;
    SigningKey& operator=(const SigningKey&) = delete;
    std::array<char, 64> bytes;
};
