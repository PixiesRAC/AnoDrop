#pragma once

#include "DhtPublisher.h"
#include "TimeUtils.h"

class HeartbeatHandler
{
  public:
    static constexpr int HEARTBEAT_TIMEOUT_SEC = static_cast<int>(TimeUtils::HEARTBEAT_TIMEOUT_SECONDS);

    HeartbeatHandler(std::shared_ptr<DhtPublisher> dhtPublisher);
    ~HeartbeatHandler() = default;

    void dispatchHeartbeatMessage(const LobbyData &lobbyData);
    bool checkLobbyHeartbeat(const LobbyData &lobbyData, std::shared_ptr<LobbyStore> lobbyStore);

  private:
    std::shared_ptr<DhtPublisher> m_dhtPublisher;

    static constexpr char HEARTBEAT_KEY[12] = "_HEARTBEAT_";
};
