#pragma once

#include "LobbyData.h"
#include "LobbyObserver.h"

#include <atomic>

class Lobby : public LobbyObserver
{
  public:
    Lobby(const LobbyData &data) : m_lobbyData(data)
    {
    }
    ~Lobby();

  private:
    LobbyData m_lobbyData;
    std::atomic<bool> m_hasUpdate{false};

  public:
    void onUpdate(const nlohmann::json &jsonPatch) override;
    std::string getLobbyName() const override
    {
        return m_lobbyData.lobby_name;
    }
    LobbyData getLobbyData() const override
    {
        return m_lobbyData;
    }
    bool consumeUpdateFlag() override
    {
        return m_hasUpdate.exchange(false);
    }
};