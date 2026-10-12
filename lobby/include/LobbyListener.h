#pragma once

#include <mutex>
#include <shared_mutex>

#include "LobbyObserver.h"

class LobbyListener
{
  public:
    LobbyListener() = default;
    ~LobbyListener() = default;

    void addObserver(std::shared_ptr<LobbyObserver> observer);
    void removeObserver(std::shared_ptr<LobbyObserver> observer);

    [[nodiscard]] std::shared_ptr<LobbyObserver> getObserver(const std::string &lobbyName);
    [[nodiscard]] std::shared_ptr<LobbyObserver> getOrCreateObserver(const std::string &lobbyName, std::function<std::shared_ptr<LobbyObserver>()> factory);
    [[nodiscard]] std::vector<std::shared_ptr<LobbyObserver>> getObservers() const
    {
        std::shared_lock<std::shared_mutex> lock(m_listenerMutex);
        return m_observers;
    }

  private:
    std::vector<std::shared_ptr<LobbyObserver>> m_observers;
    mutable std::shared_mutex m_listenerMutex;
};