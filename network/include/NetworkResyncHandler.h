#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

class DhtPublisher;
class DhtManager;
class LobbyObserver;
class LobbyStore;

class NetworkResyncHandler
{
  public:
    struct RetryTracker
    {
        std::chrono::steady_clock::time_point lastAttempt{};
        int attemptCount{0};
    };

    explicit NetworkResyncHandler(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore = nullptr);
    ~NetworkResyncHandler();

    void attach(DhtManager *dhtManager);
    void detach();

    void onPutFailed(const std::string &lobbyName, std::int64_t seqNum);
    void onNetworkRecovered();

    void retryUnconfirmedMessages(std::shared_ptr<LobbyObserver> lobbyObs);
    void retryAllUnconfirmedMessages();

  private:
    std::weak_ptr<DhtPublisher> m_dhtPublisher;
    std::shared_ptr<LobbyStore> m_lobbyStore;

    std::mutex m_retryMutex;
    mutable std::shared_mutex m_callbackMutex;
    std::unordered_map<std::string, std::unordered_map<std::int64_t, RetryTracker>> m_outboxRetryAttempts;
    DhtManager *m_attachedDhtManager{nullptr};
    std::weak_ptr<std::atomic<bool>> m_dhtManagerAlive;
    std::shared_ptr<std::atomic<bool>> m_alive{std::make_shared<std::atomic<bool>>(true)};
};
