#pragma once

#include "LobbyObserver.h"
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

class LobbyListener
{
  public:
    LobbyListener() = default;
    ~LobbyListener() = default;

    void addObserver(std::shared_ptr<LobbyObserver> observer);
    void removeObserver(std::shared_ptr<LobbyObserver> observer);

    bool hasAlreadyObserver(const std::string &lobbyName);
    std::shared_ptr<LobbyObserver> getObserver(const std::string &lobbyName);
    const std::vector<std::shared_ptr<LobbyObserver>>& getObservers() const { return m_observers; }
    void notifyObservers(const std::string &lobbyName, const nlohmann::json &jsonPatch);

  private:
    std::vector<std::shared_ptr<LobbyObserver>> m_observers;
};