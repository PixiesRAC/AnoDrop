#pragma once

#include "AckHandler.h"
#include "AnomalyHandler.h"
#include "HeartbeatHandler.h"
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

class LobbyFetcher
{
  public:
    static constexpr int STATUS_CHECK_INTERVAL_SEC = 30;
    static constexpr int ESTABLISHED_LOBBY_FETCH_INTERVAL_MS = 5000;

    struct Context
    {
        std::shared_ptr<LobbyStore> lobbyStore;
        std::shared_ptr<DhtPublisher> dhtPublisher;
        std::mutex statusCheckMutex;
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastStatusCheck;
        AnomalyHandler anomalyHandler;
        HeartbeatHandler heartbeatHandler;
        AckHandler ackHandler;

        Context(std::shared_ptr<DhtPublisher> pub, std::shared_ptr<LobbyStore> store)
            : lobbyStore(std::move(store)), dhtPublisher(std::move(pub)),
              anomalyHandler(dhtPublisher), heartbeatHandler(dhtPublisher), ackHandler(dhtPublisher)
        {
        }

        void processLobby(const std::string &lobbyName);
    };

    LobbyFetcher(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, int fetchInterval = 10000);
    ~LobbyFetcher();

    void ProcessLobbies();
    void stop();

  private:
    void fetchLoop(std::stop_token stoken);

    std::shared_ptr<Context> m_context;
    int m_fetchInterval;
    std::jthread m_fetchThread;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_nextHandshakeAttempt;
    std::mutex m_loopMutex;
    std::condition_variable_any m_loopCv;
};