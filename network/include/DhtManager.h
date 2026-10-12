#pragma once
#ifndef DHT_MANAGER_H
#define DHT_MANAGER_H

#include <atomic>
#include <chrono>
#include <functional>
#include <shared_mutex>
#include <unordered_map>

#include "AlertListener.h"
#include "LobbyStore.h"
#include "Util.h"

class DhtManager : public AlertObserver
{
  public:
    static constexpr size_t MAX_BEP44_PAYLOAD_SIZE = 1000;

    explicit DhtManager(const std::string &networkInterface = "any");
    ~DhtManager();
    void shutdown();
    void setLobbyStore(std::shared_ptr<LobbyStore> lobbyStore);
    [[nodiscard]] std::shared_ptr<LobbyStore> getLobbyStore() const;

    [[nodiscard]] bool isSessionReady() const;

    void onAlert(lt::alert *alert) override;
    void onTick() override;

    void startAlertThread();
    void waitUntilDhtIsReady();

    using DhtItemCallback = std::function<void(const lt::dht_mutable_item_alert *)>;
    bool fetchDhtItem(const std::string &rawKeyString, const std::string &lobbyName, DhtItemCallback callback, bool isSeqNum = false, std::int64_t seqNumTarget = -1);

    void registerPendingPut(const std::string &targetHex, const std::string &lobbyName, std::int64_t seqNum);

    void recoverNetwork();
    using PutFailureCallback = std::function<void(const std::string &lobbyName, std::int64_t seqNum)>;
    void setPutFailureCallback(PutFailureCallback cb);
    using NetworkRecoveredCallback = std::function<void()>;
    void setNetworkRecoveredCallback(NetworkRecoveredCallback cb);

    [[nodiscard]] std::shared_ptr<std::atomic<bool>> getAliveToken() const { return m_aliveToken; }

  private:
    friend class DhtPublisher;
    friend class DhtSignalingService;
    [[nodiscard]] lt::session *getSession() const;

    using Clock = std::chrono::steady_clock;
    void handleDhtStatsAlert(const lt::dht_stats_alert *stats);
    void handleMutableItemAlert(const lt::dht_mutable_item_alert *get_mut);
    [[nodiscard]] static std::string operationKey(const std::array<char, 32> &key, const std::string &salt);

    std::shared_ptr<lt::session> m_session;
    std::shared_ptr<LobbyStore> m_lobbyStore;

    std::atomic_bool m_isDhtReady{false};

    std::unique_ptr<AlertListener> m_alertListener;
    std::jthread m_alertThread;

    struct FetchContext
    {
        DhtItemCallback callback;
        bool isSeqNum;
        std::int64_t seqNumTarget;
        std::chrono::steady_clock::time_point createdAt{std::chrono::steady_clock::now()};
    };
    std::unordered_map<std::string, FetchContext> m_fetchCallbacks;

    mutable std::shared_mutex m_pendingPutsMutex;
    std::unordered_map<std::string, std::pair<std::string, std::int64_t>> m_pendingPuts;
    mutable std::shared_mutex m_registryMutex;

    std::chrono::steady_clock::time_point m_lastRecoveryTime{};
    std::chrono::steady_clock::time_point m_lastStatsTime{};
    std::atomic<bool> m_networkRecoveryPending{false};
    mutable std::mutex m_callbacksMutex;
    PutFailureCallback m_onPutFailure;
    NetworkRecoveredCallback m_onNetworkRecovered;
    std::shared_ptr<std::atomic<bool>> m_aliveToken{std::make_shared<std::atomic<bool>>(true)};
};
#endif
