#include "DhtManager.h"

#include <cstdlib>
#include <iostream>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/hasher.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "CryptoUtils.h"
#include "HandshakeKeyService.h"
#include "Hasher.h"
#include "KillSwitch.h"
#include "LobbyDataSerializer.h"
#include "NetworkUtils.h"

DhtManager::DhtManager(const std::string &networkInterface) : AlertObserver()
{
    SPDLOG_INFO("Initializing DhtManager session with network_interface='{}'", networkInterface);

    KillSwitch::getInstance().init(networkInterface);

    lt::settings_pack p;
    p.set_bool(lt::settings_pack::enable_dht, true);
    p.set_bool(lt::settings_pack::enable_upnp, false);
    p.set_bool(lt::settings_pack::enable_natpmp, false);
    p.set_bool(lt::settings_pack::enable_lsd, false);
    p.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);
    p.set_int(lt::settings_pack::alert_queue_size, 100000); // Prevent dropped alerts under load

    if (KillSwitch::getInstance().isEnabled())
    {
        const std::string &boundIface = KillSwitch::getInstance().getBoundInterface();
        SPDLOG_INFO("🔒 KILLSWITCH ACTIVE: Binding Libtorrent exclusively to interface '{}'", boundIface);
        p.set_str(lt::settings_pack::listen_interfaces, boundIface + ":0");
        p.set_str(lt::settings_pack::outgoing_interfaces, boundIface);
    }

    m_session = std::make_shared<lt::session>(p);
}

DhtManager::~DhtManager()
{
    SPDLOG_INFO("~DhtManager: destroying DHT session...");
    if (m_aliveToken)
    {
        m_aliveToken->store(false, std::memory_order_release);
    }
    shutdown();
}

void DhtManager::shutdown()
{
    if (m_aliveToken)
    {
        m_aliveToken->store(false, std::memory_order_release);
    }

    if (m_session)
    {
        m_session->post_dht_stats();
    }
    if (m_alertThread.joinable())
    {
        SPDLOG_INFO("DhtManager::shutdown: stopping alert thread...");
        m_alertThread.request_stop();
        m_alertThread.join();
        SPDLOG_INFO("DhtManager::shutdown: alert thread stopped.");
    }
    {
        std::lock_guard<std::mutex> lock(m_callbacksMutex);
        m_onPutFailure = nullptr;
        m_onNetworkRecovered = nullptr;
    }
    if (m_alertListener)
    {
        SPDLOG_INFO("DhtManager::shutdown: resetting alert listener...");
        m_alertListener->removeObserver(this);
        m_alertListener.reset();
    }
    {
        std::unique_lock<std::shared_mutex> lock(m_registryMutex);
        m_fetchCallbacks.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(m_pendingPutsMutex);
        m_pendingPuts.clear();
    }
    SPDLOG_INFO("DhtManager::shutdown: DHT shutdown completed.");
}

void DhtManager::setLobbyStore(std::shared_ptr<LobbyStore> lobbyStore)
{
    m_lobbyStore = lobbyStore;
}

std::shared_ptr<LobbyStore> DhtManager::getLobbyStore() const
{
    return m_lobbyStore;
}

void DhtManager::startAlertThread()
{
    m_alertListener = std::make_unique<AlertListener>(m_session);

    m_alertListener->addObserver(this);
    m_alertThread = std::jthread(&AlertListener::checkForAlerts, m_alertListener.get());
}

void DhtManager::handleDhtStatsAlert(const lt::dht_stats_alert *stats)
{
    bool is_v6 = stats->local_endpoint.address().is_v6();
    std::stringstream ss;
    ss << stats->nid;

    if (is_v6)
    {
        SPDLOG_INFO("Local Node ID (IPv6): {}", ss.str());
    }
    else
    {
        SPDLOG_INFO("Local Node ID (IPv4): {}", ss.str());
    }

    int total_nodes = 0;
    for (auto const &bucket : stats->routing_table)
    {
        total_nodes += bucket.num_nodes;
    }
    SPDLOG_INFO("DHT stats received ({}). Known nodes: {}", is_v6 ? "IPv6" : "IPv4", total_nodes);
    if (!is_v6 && total_nodes == 0 && m_isDhtReady.load(std::memory_order_acquire))
    {
        SPDLOG_WARN("DHT known nodes dropped to 0! Triggering network auto-recovery...");
        recoverNetwork();
    }
    else if (!is_v6 && total_nodes > 0 && m_networkRecoveryPending.exchange(false, std::memory_order_acq_rel))
    {
        SPDLOG_INFO("🎉 Network sockets successfully recovered with {} DHT nodes!", total_nodes);
        NetworkRecoveredCallback cb;
        {
            std::lock_guard<std::mutex> lock(m_callbacksMutex);
            cb = m_onNetworkRecovered;
        }
        if (cb)
        {
            cb();
        }
    }
    m_isDhtReady.store(true, std::memory_order_release);
    m_isDhtReady.notify_one();
}

void DhtManager::recoverNetwork()
{
    if (!m_session)
        return;

    auto now = std::chrono::steady_clock::now();
    if (m_lastRecoveryTime.time_since_epoch().count() > 0 &&
        std::chrono::duration_cast<std::chrono::seconds>(now - m_lastRecoveryTime).count() < 10)
    {
        return; // Rate limit recovery to once every 10 seconds
    }
    m_lastRecoveryTime = now;
    m_networkRecoveryPending.store(true, std::memory_order_release);

    SPDLOG_WARN("🔄 Auto-recovery triggered: Reopening network sockets and re-bootstrapping DHT...");
    m_session->reopen_network_sockets();
    m_session->add_dht_node({"router.bittorrent.com", 6881});
    m_session->add_dht_node({"dht.transmissionbt.com", 6881});
    m_session->add_dht_node({"router.utorrent.com", 6881});
    m_session->post_dht_stats();
}

void DhtManager::setPutFailureCallback(PutFailureCallback cb)
{
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_onPutFailure = std::move(cb);
}

void DhtManager::setNetworkRecoveredCallback(NetworkRecoveredCallback cb)
{
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_onNetworkRecovered = std::move(cb);
}

std::string DhtManager::operationKey(const std::array<char, 32> &key, const std::string &salt)
{
    return Util::toHex(std::string(key.data(), key.size())) + ":" + salt;
}

void DhtManager::handleMutableItemAlert(const lt::dht_mutable_item_alert *get_mut)
{
    std::string opKey = operationKey(get_mut->key, get_mut->salt);

    DhtItemCallback callback;
    {
        std::unique_lock<std::shared_mutex> lock(m_registryMutex);

        auto it = m_fetchCallbacks.find(opKey);
        if (it != m_fetchCallbacks.end())
        {
            callback = it->second.callback;

            bool isChatPayload = (!it->second.isSeqNum && it->second.seqNumTarget != -1);
            bool hasValidItem = (get_mut->item.type() != lt::entry::undefined_t);

            // Cleanup logic:
            // 1) If this is an authoritative alert (final result of DHT lookup),
            //    we MUST erase the callback to avoid memory leaks.
            // 2) If this is a chat payload and we received a defined, non-empty item,
            //    we got the message and can erase it immediately to avoid duplicate processing.
            if (get_mut->authoritative || (isChatPayload && hasValidItem))
            {
                m_fetchCallbacks.erase(it);
            }
        }
    }

    if (callback)
    {
        callback(get_mut);
    }
}

lt::session *DhtManager::getSession() const
{
    return m_session.get();
}

void DhtManager::onAlert(lt::alert *alert)
{
    if (auto *stats = lt::alert_cast<lt::dht_stats_alert>(alert))
    {
        handleDhtStatsAlert(stats);
    }
    else if (auto *put_alert = lt::alert_cast<lt::dht_put_alert>(alert))
    {
        lt::hasher h(put_alert->public_key.data(), static_cast<int>(put_alert->public_key.size()));
        if (!put_alert->salt.empty())
        {
            h.update(put_alert->salt.data(), static_cast<int>(put_alert->salt.size()));
        }
        std::string targetHex = Util::toHex(h.final().to_string());
        SPDLOG_INFO("DHT put operation finished! Target: {}", targetHex);

        std::unique_lock<std::shared_mutex> lock(m_pendingPutsMutex);
        auto it = m_pendingPuts.find(targetHex);
        if (it != m_pendingPuts.end())
        {
            std::string lobbyName = it->second.first;
            std::int64_t seqNum = it->second.second;
            m_pendingPuts.erase(it);

            if (m_lobbyStore)
            {
                if (put_alert->num_success > 0)
                {
                    SPDLOG_INFO("DHT put succeeded ({} nodes accepted it) for seq: {}", put_alert->num_success, seqNum);
                    nlohmann::json patch;
                    patch["message_sent_confirm"] = seqNum;
                    m_lobbyStore->asyncUpdateLobbyData(lobbyName, patch);
                }
                else
                {
                    SPDLOG_ERROR("DHT put failed (0 nodes accepted it) for seq: {}", seqNum);
                    recoverNetwork();
                    PutFailureCallback putFailureCb;
                    {
                        std::lock_guard<std::mutex> cbLock(m_callbacksMutex);
                        putFailureCb = m_onPutFailure;
                    }
                    if (putFailureCb)
                    {
                        putFailureCb(lobbyName, seqNum);
                    }
                }
            }
        }
        else if (put_alert->num_success == 0)
        {
            SPDLOG_WARN("DHT put failed (0 nodes accepted it) for target: {}. Triggering network recovery...", targetHex);
            recoverNetwork();
        }
    }
    else if (auto *get_mut = lt::alert_cast<lt::dht_mutable_item_alert>(alert))
    {
        handleMutableItemAlert(get_mut);
    }
    else if ([[maybe_unused]] auto *bootstrap = lt::alert_cast<lt::dht_bootstrap_alert>(alert))
    {
        SPDLOG_INFO("DHT bootstrap complete! The engine is connected to the network.");
        m_session->post_dht_stats();
        if (m_networkRecoveryPending.exchange(false, std::memory_order_acq_rel))
        {
            SPDLOG_INFO("🎉 Network sockets successfully recovered via DHT bootstrap!");
            NetworkRecoveredCallback networkRecoveredCb;
            {
                std::lock_guard<std::mutex> lock(m_callbacksMutex);
                networkRecoveredCb = m_onNetworkRecovered;
            }
            if (networkRecoveredCb)
            {
                networkRecoveredCb();
            }
        }
    }
    else if (auto *listen_ok = lt::alert_cast<lt::listen_succeeded_alert>(alert))
    {
        SPDLOG_INFO("Network listening started successfully on port: {}", listen_ok->port);
    }
    else if (auto *listen_err = lt::alert_cast<lt::listen_failed_alert>(alert))
    {
        SPDLOG_ERROR("Failed to open network port: {}", listen_err->error.message());
        KillSwitch::getInstance().handleListenFailed(listen_err->error.message());
    }
    else if (auto *dht_err = lt::alert_cast<lt::dht_error_alert>(alert))
    {
        SPDLOG_WARN("Internal DHT error: {}", dht_err->error.message());
        recoverNetwork();
    }
}

void DhtManager::onTick()
{
    KillSwitch::getInstance().checkHealth();

    auto now = std::chrono::steady_clock::now();
    if (m_session && std::chrono::duration_cast<std::chrono::seconds>(now - m_lastStatsTime).count() >= 5)
    {
        m_lastStatsTime = now;
        m_session->post_dht_stats();
    }
}

void DhtManager::waitUntilDhtIsReady()
{
    SPDLOG_INFO("waiting for DHT to be ready");
    m_isDhtReady.wait(false, std::memory_order_acquire);
}

void DhtManager::registerPendingPut(const std::string &targetHex, const std::string &lobbyName, std::int64_t seqNum)
{
    std::unique_lock<std::shared_mutex> lock(m_pendingPutsMutex);
    m_pendingPuts[targetHex] = {lobbyName, seqNum};
}

bool DhtManager::isSessionReady() const
{
    if (!m_session)
        return false;
    return true;
}

static std::string getLobbySalt(const std::string &lobbyName)
{
    auto argon2Bytes = Hasher::getInstance().generateArgon2Salt(lobbyName);
    return std::string(argon2Bytes.begin(), argon2Bytes.end());
}

bool DhtManager::fetchDhtItem(const std::string &rawKeyString, const std::string &lobbyName, DhtItemCallback callback, bool isSeqNum, std::int64_t seqNumTarget)
{
    if (!m_session || !callback)
        return false;

    std::vector<unsigned char> seed = CryptoUtils::deriveHKDF(std::vector<unsigned char>(rawKeyString.begin(), rawKeyString.end()), {}, "DHTSeed", 32).value_or(std::vector<unsigned char>());
    if (seed.empty())
    {
        SPDLOG_ERROR("Failed to derive seed");
        return false;
    }
    HandshakeKeyService::ChannelKeys info(seed);

    std::string salt = getLobbySalt(lobbyName);
    std::string opKey = operationKey(info.publicKey, salt);
    std::string pubKeyHex = Util::toHex(std::string(info.publicKey.data(), info.publicKey.size()));

    auto now = std::chrono::steady_clock::now();
    {
        std::unique_lock<std::shared_mutex> lock(m_registryMutex);

        // Prune expired callbacks (> 60s) to prevent memory leaks if alerts were lost
        std::erase_if(m_fetchCallbacks, [now](const auto &item) {
            return std::chrono::duration_cast<std::chrono::seconds>(now - item.second.createdAt).count() > 60;
        });

        m_fetchCallbacks[opKey] = FetchContext{callback, isSeqNum, seqNumTarget, now};
    }
    lt::hasher h(info.publicKey.data(), info.publicKey.size());
    if (!salt.empty())
    {
        h.update(salt.data(), static_cast<int>(salt.size()));
    }
    std::string targetIDHex = Util::toHex(h.final().to_string());

    if (isSeqNum)
    {
        SPDLOG_INFO("🔍 Polling latest Sequence Number for lobby: {}", lobbyName);
        SPDLOG_DEBUG("Raw Key String: {}, Target ID (SHA-1): {}, Public Key (Hex): {}", rawKeyString, targetIDHex, pubKeyHex);
    }
    else
    {
        if (seqNumTarget == -1)
        {
            SPDLOG_INFO("🔍 Fetching ACKs (or Heartbeat) in lobby: {}", lobbyName);
        }
        else
        {
            SPDLOG_INFO("🔍 Fetching Message payload for SeqNum {} in lobby: {}", seqNumTarget, lobbyName);
        }
        SPDLOG_DEBUG("Raw Key String: {}, Target ID (SHA-1): {}, Public Key (Hex): {}", rawKeyString, targetIDHex, pubKeyHex);
    }

    m_session->dht_get_item(info.publicKey, salt);
    return true;
}