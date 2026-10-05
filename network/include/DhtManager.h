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

    DhtManager();
    ~DhtManager();
    void shutdown();
    void setLobbyStore(std::shared_ptr<LobbyStore> lobbyStore);
    std::shared_ptr<LobbyStore> getLobbyStore() const;

    bool isSessionReady() const;

    void onAlert(lt::alert *alert) override;

    void startAlertThread();
    void waitUntilDhtIsReady();

    using DhtItemCallback = std::function<void(const lt::dht_mutable_item_alert *)>;
    bool fetchDhtItem(const std::string &rawKeyString, const std::string &lobbyName, DhtItemCallback callback, bool isSeqNum = false, std::int64_t seqNumTarget = -1);

    bool waitForPutHandshakeConfirmed(const std::string &lobbyName = "", int timeoutSeconds = 60);
    void registerPendingPut(const std::string &targetHex, const std::string &lobbyName, std::int64_t seqNum);

  private:
    friend class DhtPublisher;
    lt::session *getSession() const;

    using Clock = std::chrono::steady_clock;
    void handleDhtStatsAlert(const lt::dht_stats_alert *stats);
    void handleMutableItemAlert(const lt::dht_mutable_item_alert *get_mut);
    static std::string operationKey(const std::array<char, 32> &key, const std::string &salt);

    std::shared_ptr<lt::session> m_session;
    std::shared_ptr<LobbyStore> m_lobbyStore;

    std::atomic_bool m_isDhtReady{false};
    std::shared_ptr<std::atomic_bool> m_stopping{std::make_shared<std::atomic_bool>(false)};

    std::unique_ptr<AlertListener> m_alertListener;
    std::jthread m_alertThread;

    struct FetchContext
    {
        DhtItemCallback callback;
        bool isSeqNum;
        std::int64_t seqNumTarget;
    };
    std::unordered_map<std::string, FetchContext> m_fetchCallbacks;

    mutable std::shared_mutex m_pendingPutsMutex;
    std::unordered_map<std::string, std::pair<std::string, std::int64_t>> m_pendingPuts;
    std::condition_variable_any m_putConfirmedCv;
    mutable std::shared_mutex m_registryMutex;
};
#endif
