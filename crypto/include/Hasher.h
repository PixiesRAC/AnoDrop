#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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

    std::shared_ptr<const Config> m_config;
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
        auto replacement = std::make_shared<const Config>(config);
        std::unique_lock lock(m_cacheMutex);
        m_config = std::move(replacement);
        m_saltCache.clear();
    }

    [[nodiscard]] inline std::vector<unsigned char> generateArgon2Salt(const std::string &lobbyName)
    {
        uint32_t iterations = 5;
        SaltParams params{};
        std::shared_ptr<const Config> configSnapshot;

        {
            std::shared_lock<std::shared_mutex> rLock(m_cacheMutex);
            auto it = m_saltCache.find(lobbyName);
            if (it != m_saltCache.end())
            {
                return it->second;
            }
            configSnapshot = m_config;
            const Config *cfg = configSnapshot.get();
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
            if (configSnapshot != m_config)
                return output;
            if (m_saltCache.size() >= 256)
                m_saltCache.erase(m_saltCache.begin());
            m_saltCache[lobbyName] = output;
        }

        return output;
    }
};
