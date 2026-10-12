#pragma once

#include <openssl/evp.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <shared_mutex>

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
    mutable std::shared_mutex m_cacheMutex;
    std::unordered_map<std::string, std::vector<unsigned char>> m_saltCache;

  public:
    [[nodiscard]] static Hasher &getInstance()
    {
        static Hasher instance;
        return instance;
    }

    Hasher(const Hasher &) = delete;
    Hasher &operator=(const Hasher &) = delete;

    void setConfig(const Config &config)
    {
        std::unique_lock<std::shared_mutex> lock(m_cacheMutex);
        m_config = std::make_unique<Config>(config);
        m_saltCache.clear();
    }

    [[nodiscard]] inline std::vector<unsigned char> getHashMsg256(const std::string &rawMsg)
    {
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
        if (!context)
        {
            throw std::runtime_error("Failed to create EVP_MD_CTX");
        }

        std::vector<unsigned char> hash(static_cast<size_t>(EVP_MD_size(EVP_sha256())));
        unsigned int lengthOfHash = 0;

        if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1 ||
            EVP_DigestUpdate(context.get(), rawMsg.data(), rawMsg.size()) != 1 ||
            EVP_DigestFinal_ex(context.get(), hash.data(), &lengthOfHash) != 1)
        {
            throw std::runtime_error("SHA-256 digest failed");
        }

        hash.resize(lengthOfHash);
        return hash;
    }

    [[nodiscard]] inline std::vector<unsigned char> generateArgon2Salt(const std::string &lobbyName)
    {
        {
            std::shared_lock<std::shared_mutex> rLock(m_cacheMutex);
            auto it = m_saltCache.find(lobbyName);
            if (it != m_saltCache.end())
            {
                return it->second;
            }
        }

        uint32_t iterations = 5;
        SaltParams params{};

        {
            std::shared_lock<std::shared_mutex> rLock(m_cacheMutex);
            const Config *cfg = m_config.get();
            if (cfg)
            {
                iterations = static_cast<uint32_t>(cfg->getArgon2Iterations());
                params.extraValue = cfg->getExtraSaltDataForLobbyHandshake();
            }
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

        {
            std::unique_lock<std::shared_mutex> wLock(m_cacheMutex);
            m_saltCache[lobbyName] = output;
        }

        return output;
    }
};
