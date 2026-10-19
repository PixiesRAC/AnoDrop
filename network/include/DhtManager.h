#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <unordered_map>

#include "AlertListener.h"
#include "DhtTransport.h"
#include "ThreadAffinity.h"
#include "Util.h"

class DhtManager : public AlertObserver, public DhtTransport
{
  public:
    static constexpr size_t MAX_BEP44_PAYLOAD_SIZE = 1000;

    explicit DhtManager(const std::string &networkInterface = "any");
    ~DhtManager();
    // Joining/destruction belongs to the owner; requestStop is thread-safe after startup.
    void requestStop();
    void shutdown();

    [[nodiscard]] bool hasSession() const;

    void onAlert(lt::alert *alert) override;
    void onTick() override;

    void startAlertThread();
    bool waitUntilDhtIsReady(std::chrono::seconds timeout = std::chrono::seconds(30), std::stop_token stop = {});

    bool fetchDhtItem(const std::string &rawKeyString, const std::string &lobbyName, DhtItemCallback callback, DhtRequest request = {}) override;

    std::optional<std::uint64_t> registerPendingPut(const std::string &targetHex, const std::string &lobbyName, std::int64_t seqNum, PutCompletion completion = {});
    void cancelPendingPut(const std::string &targetHex, std::uint64_t reservation);
    void setFetchLimit(std::size_t limit)
    {
        m_fetchLimit = limit;
    }

    bool publishHandshake(const std::string &lobby, const std::string &payload,
                          std::int64_t sequence, PutCompletion completion) override;
    bool publishChatMessage(const ChatPublishParams &params,
                            PutCompletion completion = {}) override;
    bool publishSequenceAnnouncement(const ChatPublishParams &params) override;
    bool publishAnomaly(const std::string &lobby, const std::string &reason) override;
    bool publishHeartbeat(int offsetSeconds, const std::string &lobby, const std::string &key) override;
    bool publishAck(const std::string &message, const std::string &lobby, const std::string &key, int offsetSeconds) override;
    using PutFailureCallback = std::function<void(const std::string &lobbyName, std::int64_t seqNum)>;
    void setPutFailureCallback(PutFailureCallback cb);
    using NetworkRecoveredCallback = std::function<void()>;
    void setNetworkRecoveredCallback(NetworkRecoveredCallback cb);

  private:
    void recoverNetwork();
    friend class DhtManagerTestAccess;
    // Offline registry fixture: no libtorrent session and no network initialization.
    explicit DhtManager(std::nullptr_t)
    {
    }
    ThreadAffinity m_ownerThread;
    [[nodiscard]] lt::session *getSession() const;

    using Clock = std::chrono::steady_clock;
    void handleDhtStatsAlert(const lt::dht_stats_alert *stats);
    void handleDhtPutAlert(const lt::dht_put_alert *alert);
    void handleMutableItemAlert(const lt::dht_mutable_item_alert *get_mut);
    void handleDhtBootstrapAlert(const lt::dht_bootstrap_alert *alert);
    void handleListenSucceededAlert(const lt::listen_succeeded_alert *alert);
    void handleListenFailedAlert(const lt::listen_failed_alert *alert);
    void handleDhtErrorAlert(const lt::dht_error_alert *alert);
    void handleMutableItemResult(const std::array<char, 32> &publicKey, const std::string &salt,
                                 std::int64_t sequence, bool authoritative, const lt::entry &value);
    [[nodiscard]] static std::string operationKey(const std::array<char, 32> &key, const std::string &salt);

    std::shared_ptr<lt::session> m_session;
    std::mutex m_readyMutex;
    std::condition_variable_any m_readyCv;

    std::atomic_bool m_isDhtReady{false};

    std::unique_ptr<AlertListener> m_alertListener;
    std::stop_source m_stopSource;
    std::jthread m_alertThread;

    void cancelPendingGet(const std::string &key, std::uint64_t reservation);
    std::uint64_t m_nextGetReservation{0}; // Under m_registryMutex.
    std::uint64_t m_nextPutReservation{0}; // Under m_pendingPutsMutex.

    struct FetchContext
    {
        std::uint64_t reservation;
        std::vector<DhtItemCallback> callbacks;
        std::string lobby;
        DhtRequest request;
        Clock::time_point createdAt{Clock::now()};
    };
    std::unordered_map<std::string, FetchContext> m_fetchCallbacks;
    std::unordered_map<std::string, Clock::time_point> m_quarantine;
    std::atomic<std::size_t> m_fetchLimit{5};

    struct PendingPut
    {
        std::uint64_t reservation;
        std::string lobby;
        std::int64_t sequence;
        PutCompletion completion;
        Clock::time_point createdAt{Clock::now()};
    };
    struct PublicationLogContext { std::string lobby; std::string role; };
    // Bounded diagnostic cache only; never participates in transport decisions.
    std::unordered_map<std::string, PublicationLogContext> m_publicationLogContexts;
    void rememberPublicationLog(const std::string &target, const std::string &lobby, const std::string &role);
    mutable std::mutex m_pendingPutsMutex;
    std::unordered_map<std::string, PendingPut> m_pendingPuts;
    mutable std::mutex m_registryMutex;

    std::map<lt::udp::endpoint, int> m_ipv4EndpointNodes;
    std::chrono::steady_clock::time_point m_lastRecoveryTime{};
    std::chrono::steady_clock::time_point m_lastStatsTime{};
    std::atomic<bool> m_networkRecoveryPending{false};
    mutable std::mutex m_callbacksMutex;
    PutFailureCallback m_onPutFailure;
    NetworkRecoveredCallback m_onNetworkRecovered;
    std::atomic<bool> m_alive{true};
};
