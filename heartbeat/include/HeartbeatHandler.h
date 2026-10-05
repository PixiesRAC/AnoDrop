#pragma once

#include "DhtPublisher.h"

class HeartbeatHandler
{
  public:
    static constexpr int HEARTBEAT_TIMEOUT_SEC = 300; // Renamed from MAX_HEARTBEAT_DISCONNECTION

    HeartbeatHandler(std::shared_ptr<DhtPublisher> dhtPublisher);
    ~HeartbeatHandler() = default;

    void dispatchHeartbeatMessage(const LobbyData &lobbyData);
    void checkLobbyHeartbeat(const LobbyData &lobbyData, std::shared_ptr<LobbyStore> lobbyStore);

  private:
    std::shared_ptr<DhtPublisher> m_dhtPublisher;

    static constexpr char HEARTBEAT_KEY[12] = "_HEARTBEAT_";
};
