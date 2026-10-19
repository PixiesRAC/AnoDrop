#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

class DhtPublisher;
class DhtManager;
class LobbyObserver;
class LobbyStore;

class NetworkResyncHandler : public std::enable_shared_from_this<NetworkResyncHandler>
{
  public:
    struct RetryTracker
    {
        std::chrono::steady_clock::time_point lastAttempt{};
        int attemptCount{0};
        std::uint64_t reservation{0};
    };

    explicit NetworkResyncHandler(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore);
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
    std::uint64_t m_nextReservation{0};
    std::unordered_map<std::string, std::int64_t> m_outboxCursor;
    std::unordered_map<std::string, std::unordered_map<std::int64_t, RetryTracker>> m_outboxRetryAttempts;
    std::atomic<std::uint64_t> m_callbackGeneration{0};
    std::atomic<bool> m_alive{true};
};
