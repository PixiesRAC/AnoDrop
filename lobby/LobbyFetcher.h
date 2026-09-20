#pragma once

#include "core/DhtPublisher.h"
#include "lobby/LobbyStore.h"

#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <chrono>

class LobbyFetcher
{
  public:
    LobbyFetcher(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, int fetchInterval = 10000);
    ~LobbyFetcher() = default;

    void ProcessLobbies();
    void stop();

  private:
    void fetchLoop(std::stop_token stoken);
    void processLobby(std::string lobbyName);

    std::shared_ptr<LobbyStore> m_lobbyStore;

    int m_fetchInterval;
    std::jthread m_fetchThread;
    std::shared_ptr<DhtPublisher> m_dhtPublisher;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_lastHandshakePoll;
};