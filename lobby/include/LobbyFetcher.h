#pragma once
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "AckHandler.h"
#include "AnomalyHandler.h"
#include "HeartbeatHandler.h"
#include "ThreadAffinity.h"
#include "TimeUtils.h"

class LobbyFetcher
{
  public:
    static constexpr int STATUS_CHECK_INTERVAL_SEC = 20;
    static constexpr int ESTABLISHED_LOBBY_FETCH_INTERVAL_MS = TimeUtils::ESTABLISHED_LOBBY_FETCH_INTERVAL_MS;

  private:
    enum class StatusOperation : std::size_t
    {
        AnomalyRead,
        HeartbeatWrite,
        HeartbeatRead,
        Count
    };

    struct Context
    {
        std::shared_ptr<LobbyStore> lobbyStore;
        std::shared_ptr<DhtPublisher> dhtPublisher;
        std::mutex statusCheckMutex;
        std::unordered_map<std::string, std::array<std::chrono::steady_clock::time_point, static_cast<std::size_t>(StatusOperation::Count)>> lastStatusCheck;
        AnomalyHandler anomalyHandler;
        HeartbeatHandler heartbeatHandler;
        AckHandler ackHandler;

        Context(std::shared_ptr<DhtPublisher> pub, std::shared_ptr<LobbyStore> store)
            : lobbyStore(std::move(store)), dhtPublisher(std::move(pub)), anomalyHandler(dhtPublisher), heartbeatHandler(dhtPublisher), ackHandler(dhtPublisher)
        {
        }

        void processLobby(const std::string &lobbyName);
    };

  public:
    LobbyFetcher(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, int fetchInterval = TimeUtils::DEFAULT_FETCH_INTERVAL_MS);
    ~LobbyFetcher();

    void ProcessLobbies();
    // Joining/destruction belongs to the owner; requestStop is thread-safe after startup.
    void requestStop();
    void stop();

  private:
    ThreadAffinity m_ownerThread;
    void fetchLoop(std::stop_token stoken);

    std::shared_ptr<Context> m_context;
    int m_fetchInterval;
    std::stop_source m_stopSource;
    std::jthread m_fetchThread;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_nextHandshakeAttempt;
    std::mutex m_loopMutex;
    std::condition_variable_any m_loopCv;
};
