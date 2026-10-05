#pragma once

#include "AckHandler.h"
#include "AnomalyHandler.h"
#include "HeartbeatHandler.h"

class LobbyFetcher
{
  public:
    static constexpr int STATUS_CHECK_INTERVAL_SEC = 30;
    static constexpr int ESTABLISHED_LOBBY_FETCH_INTERVAL_MS = 5000;

    LobbyFetcher(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, int fetchInterval = 10000);
    ~LobbyFetcher();

    void ProcessLobbies();
    void stop();

  private:
    void fetchLoop(std::stop_token stoken);
    void processLobby(std::string lobbyName);

    std::shared_ptr<LobbyStore> m_lobbyStore;

    int m_fetchInterval;
    std::jthread m_fetchThread;
    std::shared_ptr<DhtPublisher> m_dhtPublisher;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_lastStatusCheck;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_nextHandshakeAttempt;
    AnomalyHandler anomalyHandler;
    HeartbeatHandler heartbeatHandler;
    AckHandler ackHandler;
};