#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "DhtManager.h"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/hasher.hpp>
#include <spdlog/spdlog.h>

#include "HandshakeKeyService.h"
#include "Hasher.h"
#include "CryptoUtils.h"

DhtManager::DhtManager() : AlertObserver()
{
    SPDLOG_INFO("Initializing DhtManager session");
    lt::settings_pack p;
    p.set_bool(lt::settings_pack::enable_dht, true);
    p.set_bool(lt::settings_pack::enable_upnp, false);
    p.set_bool(lt::settings_pack::enable_natpmp, false);
    p.set_bool(lt::settings_pack::enable_lsd, false);
    p.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);
    p.set_int(lt::settings_pack::alert_queue_size, 100000); // Prevent dropped alerts under load

    m_session = std::make_shared<lt::session>(p);
}

DhtManager::~DhtManager()
{
    SPDLOG_INFO("~DhtManager: destroying DHT session...");
    if (m_alertThread.joinable())
    {
        m_alertThread.request_stop();
        m_alertThread.join();
    }
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
    m_isDhtReady.store(true, std::memory_order_release);
    m_isDhtReady.notify_one();
}

void DhtManager::handleMutableItemAlert(const lt::dht_mutable_item_alert *get_mut)
{
    std::string pubKeyHex = Util::toHex(std::string(get_mut->key.data(), get_mut->key.size()));

    DhtItemCallback callback;
    {
        std::unique_lock<std::shared_mutex> lock(m_registryMutex);

        auto it = m_fetchCallbacks.find(pubKeyHex);
        if (it != m_fetchCallbacks.end())
        {
            callback = it->second.callback;

            // Remove from registry immediately to prevent duplicate message processing
            // (Libtorrent can trigger multiple alerts for the same DHT fetch).
            // We ONLY do this for actual chat payload items (seqNumTarget != -1).
            // For mutable polling items (Heartbeat, Ack, Anomaly), we MUST keep the callback
            // active to process all responses and guarantee we receive the freshest value.
            if (!it->second.isSeqNum && it->second.seqNumTarget != -1)
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
                }
            }
        }
        lock.unlock(); // Unlock before notify to avoid immediately blocking the awakened thread
        m_putConfirmedCv.notify_all();
    }
    else if (auto *get_mut = lt::alert_cast<lt::dht_mutable_item_alert>(alert))
    {
        handleMutableItemAlert(get_mut);
    }
    else if ([[maybe_unused]] auto *bootstrap = lt::alert_cast<lt::dht_bootstrap_alert>(alert))
    {
        SPDLOG_INFO("DHT bootstrap complete! The engine is connected to the network.");
        m_session->post_dht_stats();
    }
    else if (auto *listen_ok = lt::alert_cast<lt::listen_succeeded_alert>(alert))
    {
        SPDLOG_INFO("Network listening started successfully on port: {}", listen_ok->port);
    }
    else if (auto *listen_err = lt::alert_cast<lt::listen_failed_alert>(alert))
    {
        SPDLOG_ERROR("Failed to open network port: {}", listen_err->error.message());
    }
    else if (auto *dht_err = lt::alert_cast<lt::dht_error_alert>(alert))
    {
        SPDLOG_WARN("Internal DHT error: {}", dht_err->error.message());
    }
}

void DhtManager::waitUntilDhtIsReady()
{
    SPDLOG_INFO("waiting for DHT to be ready");
    m_isDhtReady.wait(false, std::memory_order_acquire);
}



bool DhtManager::waitForPutHandshakeConfirmed(const std::string &lobbyName, int timeoutSeconds)
{
    std::shared_lock<std::shared_mutex> lock(m_pendingPutsMutex);
    
    auto hasPendingForLobby = [this, lobbyName]() {
        for (const auto& pair : m_pendingPuts) {
            if (lobbyName.empty() || pair.second.first == lobbyName) {
                return true;
            }
        }
        return false;
    };

    SPDLOG_INFO("waiting for put handshake confirmed for {}", lobbyName.empty() ? "all" : lobbyName);
    
    bool success = m_putConfirmedCv.wait_for(lock, std::chrono::seconds(timeoutSeconds), [&]() {
        return !hasPendingForLobby();
    });

    if (!success)
    {
        SPDLOG_WARN("waitForPutHandshakeConfirmed timed out for {} after {} seconds", lobbyName.empty() ? "unknown" : lobbyName, timeoutSeconds);
    }
    return success;
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
    if (seed.empty()) { SPDLOG_ERROR("Failed to derive seed"); return false; }
    HandshakeKeyService::ChannelKeys info(seed);

    std::string salt = getLobbySalt(lobbyName);

    std::string pubKeyHex = Util::toHex(std::string(info.publicKey.data(), info.publicKey.size()));

    {
        std::unique_lock<std::shared_mutex> lock(m_registryMutex);
        m_fetchCallbacks[pubKeyHex] = FetchContext{callback, isSeqNum, seqNumTarget};
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