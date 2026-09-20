#include "network/DhtManager.h"

#include <chrono>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <spdlog/spdlog.h>
#include <thread>

#include "crypto/HandshakeKeyService.h"
#include "crypto/CryptoUtils.h"
#include "crypto/Hasher.h"
#include "lobby/FakeTimezonePool.h"
#include "lobby/LobbyStore.h"
#include <libtorrent/hasher.hpp>

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

    {
        std::lock_guard<std::mutex> lock(m_nodeIdMutex);
        if (is_v6)
        {
            m_nodeIdIpv6 = stats->nid;
            SPDLOG_INFO("Local Node ID (IPv6): {}", ss.str());
        }
        else
        {
            m_nodeIdIpv4 = stats->nid;
            SPDLOG_INFO("Local Node ID (IPv4): {}", ss.str());
        }
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
        std::lock_guard<std::mutex> lock(m_registryMutex);

        auto it = m_fetchCallbacks.find(pubKeyHex);
        if (it != m_fetchCallbacks.end())
        {
            callback = it->second.callback;

            // Remove from registry immediately to prevent duplicate message processing
            // (Libtorrent can trigger multiple alerts for the same DHT fetch)
            if (!it->second.isSeqNum)
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

std::shared_ptr<lt::session> DhtManager::getSharedSession() const
{
    return m_session;
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

        std::lock_guard<std::mutex> lock(m_pendingPutsMutex);
        auto it = m_pendingPuts.find(targetHex);
        if (it != m_pendingPuts.end())
        {
            std::string lobbyName = it->second.first;
            std::int64_t seqNum = it->second.second;
            m_pendingPuts.erase(it);

            if (m_lobbyStore)
            {
                nlohmann::json patch;
                patch["message_sent_confirm"] = seqNum;
                m_lobbyStore->asyncUpdateLobbyData(lobbyName, patch);
            }
        }

        m_putConfirmedSem.release();
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

void DhtManager::resetPutConfirmed()
{
    while (m_putConfirmedSem.try_acquire())
    {
    }
}

void DhtManager::waitForPutConfirmed(const std::string &lobbyName)
{
    resetPutConfirmed();
    if (!lobbyName.empty())
    {
        SPDLOG_INFO("waiting for put confirmed for {}", lobbyName);
    }
    else
    {
        SPDLOG_INFO("waiting for put confirmed");
    }
    // WARNING: m_putConfirmedSem.acquire() can cause a deadlock if the DHT put fails silently or network is disconnected. Consider using a timeout in the future.
    m_putConfirmedSem.acquire(); // TODO: We need to improve this and use try_acquire_for to prevent deadlocks
}

void DhtManager::registerPendingPut(const std::string& targetHex, const std::string& lobbyName, std::int64_t seqNum)
{
    std::lock_guard<std::mutex> lock(m_pendingPutsMutex);
    m_pendingPuts[targetHex] = {lobbyName, seqNum};
}



bool DhtManager::isSessionReady() const
{
    if (!m_session)
        return false;
    return true;
}

std::optional<lt::sha1_hash> DhtManager::getLocalNodeId(IpProtocol protocol) const
{
    std::lock_guard<std::mutex> lock(m_nodeIdMutex);
    std::optional<lt::sha1_hash> result = (protocol == IpProtocol::IPv4) ? m_nodeIdIpv4 : m_nodeIdIpv6;
    if (result && result->is_all_zeros())
    {
        return std::nullopt;
    }
    return result;
}

void DhtManager::fetchDhtItem(const std::string &rawKeyString, const std::string &lobbyName, DhtItemCallback callback, bool isSeqNum, std::int64_t seqNumTarget)
{
    if (!m_session || !callback)
        return;

    std::vector<unsigned char> seed = Hasher::getInstance().getHashMsg256(rawKeyString);
    HandshakeKeyService::ChannelKeys info(seed);

    auto argon2Bytes = Hasher::getInstance().generateArgon2Salt(lobbyName);
    std::string salt(argon2Bytes.begin(), argon2Bytes.end());

    std::string pubKeyHex = Util::toHex(std::string(info.publicKey.data(), info.publicKey.size()));

    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_fetchCallbacks[pubKeyHex] = FetchContext{callback, isSeqNum};
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
        SPDLOG_INFO("🔍 Fetching Message payload for SeqNum {} in lobby: {}", seqNumTarget, lobbyName);
        SPDLOG_DEBUG("Raw Key String: {}, Target ID (SHA-1): {}, Public Key (Hex): {}", rawKeyString, targetIDHex, pubKeyHex);
    }

    m_session->dht_get_item(info.publicKey, salt);
}