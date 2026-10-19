#pragma once

#include <map>
#include <mutex>
#include <shared_mutex>

#include "LobbyObserver.h"

class LobbyRegistry
{
  public:
    LobbyRegistry() = default;
    ~LobbyRegistry() = default;

    void add(std::shared_ptr<LobbyObserver> observer);

    [[nodiscard]] std::shared_ptr<LobbyObserver> find(const std::string &lobbyName);
    [[nodiscard]] std::shared_ptr<LobbyObserver> getOrCreate(const std::string &lobbyName, std::function<std::shared_ptr<LobbyObserver>()> factory);
    [[nodiscard]] std::vector<std::shared_ptr<LobbyObserver>> all() const
    {
        std::shared_lock<std::shared_mutex> lock(m_registryMutex);
        std::vector<std::shared_ptr<LobbyObserver>> result;
        result.reserve(m_lobbies.size());
        for (const auto &[name, observer] : m_lobbies)
            result.push_back(observer);
        return result;
    }

  private:
    std::map<std::string, std::shared_ptr<LobbyObserver>> m_lobbies;
    mutable std::shared_mutex m_registryMutex;
};