#pragma once

#include "core/Config.h"
#include "crypto/SaltParams.h"

#include <cstdint>
#include <mutex>
#include <openssl/evp.h>
#include <stdexcept>
#include <string>
#include <vector>

extern "C"
{
#include <argon2.h>
}

class Hasher
{
  private:
    Hasher()
    {
        m_context = EVP_MD_CTX_new();
        if (m_context == nullptr)
        {
            throw std::runtime_error("Failed to create EVP_MD_CTX");
        }
    }

    ~Hasher()
    {
        if (m_context != nullptr)
        {
            EVP_MD_CTX_free(m_context);
        }
    }

    EVP_MD_CTX *m_context;
    std::mutex m_mutex;

    const Config *m_config = nullptr;

  public:
    static Hasher &getInstance()
    {
        static Hasher instance;
        return instance;
    }

    Hasher(const Hasher &) = delete;
    Hasher &operator=(const Hasher &) = delete;

    void setConfig(const Config &config)
    {
        m_config = &config;
    }

    inline std::vector<unsigned char> getHashMsg256(const std::string &rawMsg)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (EVP_DigestInit_ex(m_context, EVP_sha256(), nullptr) != 1)
        {
            throw std::runtime_error("Failed to initialize SHA-256 digest");
        }

        if (EVP_DigestUpdate(m_context, rawMsg.c_str(), rawMsg.length()) != 1)
        {
            throw std::runtime_error("Failed to update SHA-256 digest");
        }

        std::vector<unsigned char> hash(EVP_MD_size(EVP_sha256()));
        unsigned int lengthOfHash = 0;

        if (EVP_DigestFinal_ex(m_context, hash.data(), &lengthOfHash) != 1)
        {
            throw std::runtime_error("Failed to finalize SHA-256 digest");
        }

        hash.resize(lengthOfHash);
        return hash;
    }

    inline std::vector<unsigned char> generateArgon2Salt(const std::string &lobbyName)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        constexpr uint32_t defaultIterations = 5;
        uint32_t iterations = m_config ? static_cast<uint32_t>(m_config->getArgon2Iterations()) : defaultIterations;

        SaltParams params{};
        if (m_config)
        {
            params.extraValue = m_config->getExtraSaltDataForLobbyHandshake();
        }
        std::vector<uint8_t> argon2Salt = params.serialize();

        constexpr uint32_t memoryCost = 1 << 16;
        constexpr uint32_t parallelism = 1;
        constexpr uint32_t hashLength = 32;

        std::vector<unsigned char> output(hashLength);

        int result = argon2id_hash_raw(iterations, memoryCost, parallelism, lobbyName.data(), lobbyName.size(), argon2Salt.data(), argon2Salt.size(), output.data(), hashLength);

        if (result != ARGON2_OK)
        {
            throw std::runtime_error(std::string("Argon2id failed: ") + argon2_error_message(result));
        }

        return output;
    }
};
