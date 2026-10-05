#pragma once

#include <openssl/evp.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>

#include "Config.h"
#include "SaltParams.h"

extern "C"
{
#include <argon2.h>
}

class Hasher
{
  private:
    Hasher() = default;
    ~Hasher() = default;

    std::unique_ptr<Config> m_config;

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
        // On stocke une copie sur le tas pour éviter le dangling pointer dans les tests
        m_config = std::make_unique<Config>(config);
    }

    inline std::vector<unsigned char> getHashMsg256(const std::string &rawMsg)
    {
        EVP_MD_CTX *context = EVP_MD_CTX_new();
        if (context == nullptr)
        {
            throw std::runtime_error("Failed to create EVP_MD_CTX");
        }

        std::vector<unsigned char> hash(EVP_MD_size(EVP_sha256()));
        unsigned int lengthOfHash = 0;

        if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1 ||
            EVP_DigestUpdate(context, rawMsg.c_str(), rawMsg.length()) != 1 ||
            EVP_DigestFinal_ex(context, hash.data(), &lengthOfHash) != 1)
        {
            EVP_MD_CTX_free(context);
            throw std::runtime_error("SHA-256 digest failed");
        }

        EVP_MD_CTX_free(context);
        hash.resize(lengthOfHash);
        return hash;
    }

    inline std::vector<unsigned char> generateArgon2Salt(const std::string &lobbyName)
    {
        uint32_t iterations = 5;
        SaltParams params{};

        const Config *cfg = m_config.get();
        if (cfg)
        {
            iterations = static_cast<uint32_t>(cfg->getArgon2Iterations());
            params.extraValue = cfg->getExtraSaltDataForLobbyHandshake();
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
