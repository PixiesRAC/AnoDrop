#pragma once

#include "network/AlertListener.h"
#include "network/AlertObserver.h"
#include "utils/Util.h"

#include "lobby/LobbyObserver.h"
#include "lobby/LobbyStore.h"
#include <atomic>
#include <libtorrent/session.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <semaphore>
#include <thread>
#include <unordered_map>
#include <functional>

class DhtManager : public AlertObserver
{
  public:
    DhtManager();
    ~DhtManager();
    void setLobbyStore(std::shared_ptr<LobbyStore> lobbyStore);
    std::shared_ptr<LobbyStore> getLobbyStore() const;

    lt::session *getSession() const;
    std::shared_ptr<lt::session> getSharedSession() const;

    bool isSessionReady() const;

    void initializeDht();

    std::optional<lt::sha1_hash> getLocalNodeId(IpProtocol protocol = IpProtocol::IPv4) const;

    void onAlert(lt::alert *alert) override;
    void startAlertThread();
    void waitUntilDhtIsReady();

    void resetPutConfirmed();
    void waitForPutConfirmed(const std::string &context = "");
    void registerPendingPut(const std::string& targetHex, const std::string& lobbyName, std::int64_t seqNum);


    /* HANDSHAKE */

    using DhtItemCallback = std::function<void(const lt::dht_mutable_item_alert*)>;
    void fetchDhtItem(const std::string &rawKeyString, const std::string &lobbyName, DhtItemCallback callback, bool isSeqNum = false, std::int64_t seqNumTarget = -1);

    /* END HANDSHAKE*/

  private:
    void handleDhtStatsAlert(const lt::dht_stats_alert *stats);
    void handleMutableItemAlert(const lt::dht_mutable_item_alert *get_mut);

    std::shared_ptr<lt::session> m_session;
    std::shared_ptr<LobbyStore> m_lobbyStore;
    std::optional<lt::sha1_hash> m_nodeIdIpv4;
    std::optional<lt::sha1_hash> m_nodeIdIpv6;

    std::mutex m_pendingPutsMutex;
    std::unordered_map<std::string, std::pair<std::string, std::int64_t>> m_pendingPuts;
    std::atomic_bool m_isDhtReady{false};
    std::binary_semaphore m_putConfirmedSem{0};
    mutable std::mutex m_nodeIdMutex;

    std::unique_ptr<AlertListener> m_alertListener;
    std::jthread m_alertThread;

    struct FetchContext
    {
        DhtItemCallback callback;
        bool isSeqNum;
    };
    std::unordered_map<std::string, FetchContext> m_fetchCallbacks;
    std::mutex m_registryMutex;
};
