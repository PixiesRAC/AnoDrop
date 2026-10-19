#include "LogEvents.h"
#include "DhtManager.h"

#include <cstdlib>
#include <iostream>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/hasher.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <spdlog/spdlog.h>

#include "CryptoUtils.h"
#include "DhtAddress.h"
#include "DhtLog.h"
#include "DhtMessageCodec.h"
#include "Dropper.h"
#include "HandshakeKeyService.h"
#include "Hasher.h"
#include "KillSwitch.h"
#include "LobbyDataSerializer.h"
#include "NetworkUtils.h"

DhtManager::DhtManager(const std::string &networkInterface) : AlertObserver()
{
    SPDLOG_INFO("[DHT] Initializing DhtManager session with network_interface='{}'", networkInterface);

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
        SPDLOG_INFO("[DHT] KILLSWITCH ACTIVE: Binding Libtorrent exclusively to interface '{}'", boundIface);
        p.set_str(lt::settings_pack::listen_interfaces, boundIface + ":0");
        p.set_str(lt::settings_pack::outgoing_interfaces, boundIface);
    }

    m_session = std::make_shared<lt::session>(p);
}

DhtManager::~DhtManager()
{
    SPDLOG_INFO("[DHT] ~DhtManager: destroying DHT session...");
    shutdown();
}

void DhtManager::shutdown()
{
    m_ownerThread.requireOwner();
    requestStop();
    if (m_session)
    {
        m_session->post_dht_stats();
    }
    if (m_alertThread.joinable())
    {
        SPDLOG_INFO("[DHT] DhtManager::shutdown: stopping alert thread...");
        m_alertThread.join();
        SPDLOG_INFO("[DHT] DhtManager::shutdown: alert thread stopped.");
    }
    setPutFailureCallback({});
    setNetworkRecoveredCallback({});
    if (m_alertListener)
    {
        SPDLOG_INFO("[DHT] DhtManager::shutdown: resetting alert listener...");
        m_alertListener.reset();
    }
    decltype(m_fetchCallbacks) cancelled;
    decltype(m_pendingPuts) puts;
    {
        std::unique_lock lock(m_registryMutex);
        cancelled.swap(m_fetchCallbacks);
        m_quarantine.clear();
    }
    {
        std::unique_lock lock(m_pendingPutsMutex);
        puts.swap(m_pendingPuts);
    }
    for (auto &[key, context] : cancelled)
    {
        for (auto &callback : context.callbacks)
        {
            try
            {
                callback({DhtFetchStatus::Cancelled, {}, 0});
            }
            catch (...)
            {
                SPDLOG_WARN("[DHT] Cancelled GET completion threw");
            }
        }
    }
    for (auto &[key, pending] : puts)
    {
        try
        {
            if (pending.completion)
                pending.completion(false);
        }
        catch (...)
        {
            SPDLOG_WARN("[DHT] Cancelled PUT completion threw");
        }
    }
    SPDLOG_INFO("[DHT] DhtManager::shutdown: DHT shutdown completed.");
}

void DhtManager::startAlertThread()
{
    m_ownerThread.requireOwner();
    if (m_alertThread.joinable())
        return;
    if (!m_alive.load())
        throw std::logic_error("Cannot restart a stopped DHT manager");
    m_alertListener = std::make_unique<AlertListener>(m_session, *this);
    m_alertThread = std::jthread([listener = m_alertListener.get(), stoken = m_stopSource.get_token()]
                                 { listener->checkForAlerts(stoken); });
}

void DhtManager::handleDhtStatsAlert(const lt::dht_stats_alert *stats)
{
    bool is_v6 = stats->local_endpoint.address().is_v6();
    std::stringstream ss;
    ss << stats->nid;

    int total_nodes = 0;
    for (auto const &bucket : stats->routing_table)
    {
        total_nodes += bucket.num_nodes;
    }
    SPDLOG_INFO("[DHT] Routing table | ip_version={} node_id={} local_address={} local_port={} known_nodes={}",
                is_v6 ? "IPv6" : "IPv4", ss.str(), stats->local_endpoint.address().to_string(), stats->local_endpoint.port(), total_nodes);
    if (!is_v6)
    {
        m_ipv4EndpointNodes[stats->local_endpoint] = total_nodes;
        int aggregateIpv4Nodes = 0;
        for (const auto &[endpoint, count] : m_ipv4EndpointNodes)
        {
            aggregateIpv4Nodes += count;
        }
        if (aggregateIpv4Nodes == 0 && m_isDhtReady.load(std::memory_order_acquire))
        {
            SPDLOG_WARN("[DHT] DHT known nodes dropped to 0! Triggering network auto-recovery...");
            recoverNetwork();
        }
        else if (aggregateIpv4Nodes > 0 && m_networkRecoveryPending.exchange(false, std::memory_order_acq_rel))
        {
            SPDLOG_INFO("[DHT] Network sockets successfully recovered with {} DHT nodes!", aggregateIpv4Nodes);
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
    }
    if (total_nodes > 0)
    {
        std::lock_guard lock(m_readyMutex);
        m_isDhtReady.store(true, std::memory_order_release);
        m_readyCv.notify_all();
    }
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
    m_ipv4EndpointNodes.clear();
    m_networkRecoveryPending.store(true, std::memory_order_release);

    SPDLOG_WARN("[DHT] Auto-recovery triggered: Reopening network sockets and re-bootstrapping DHT...");
    m_session->reopen_network_sockets();
    m_session->add_dht_node({"router.bittorrent.com", 6881});
    m_session->add_dht_node({"dht.transmissionbt.com", 6881});
    m_session->add_dht_node({"router.utorrent.com", 6881});
    m_session->post_dht_stats();
}

void DhtManager::setPutFailureCallback(PutFailureCallback cb)
{
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_onPutFailure.swap(cb); // Destroy the old callback after releasing the mutex.
}

void DhtManager::setNetworkRecoveredCallback(NetworkRecoveredCallback cb)
{
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_onNetworkRecovered.swap(cb); // Destroy the old callback after releasing the mutex.
}

std::string DhtManager::operationKey(const std::array<char, 32> &key, const std::string &salt)
{
    return Util::toHex(key) + ":" + salt;
}

void DhtManager::handleMutableItemAlert(const lt::dht_mutable_item_alert *item)
{
    handleMutableItemResult(item->key, item->salt, item->seq, item->authoritative, item->item);
}

void DhtManager::handleMutableItemResult(const std::array<char, 32> &publicKey, const std::string &salt,
                                       std::int64_t sequence, bool authoritative, const lt::entry &value)
{
    const auto key = operationKey(publicKey, salt);
    std::string lobby = "<untracked>";
    std::string role = "untracked";
    std::uint64_t requestId = 0;
    std::size_t callbackCount = 0;
    {
        std::lock_guard lock(m_registryMutex);
        if (const auto it = m_fetchCallbacks.find(key); it != m_fetchCallbacks.end()) {
            lobby = it->second.lobby;
            requestId = it->second.reservation;
            callbackCount = it->second.callbacks.size();
            const auto &request = it->second.request;
            role = !request.logRole.empty() ? request.logRole :
                   request.kind == DhtFetchKind::Handshake ? "handshake" :
                   request.kind == DhtFetchKind::Message ? "message" :
                   request.kind == DhtFetchKind::Sequence ? "sequence_announcement" : "signaling";
        }
    }
    const auto logResult = [&](auto level) {
        SPDLOG_LOGGER_CALL(spdlog::default_logger_raw(), level,
            "[DHT] Lookup result: {} | lobby={} target_sha1={} request_id={} found={} dht_seq={} authoritative={} callbacks={}",
            role, nlohmann::json(lobby).dump(), network::logging::target(publicKey, salt),
            requestId, value.type() == lt::entry::string_t, sequence, authoritative, callbackCount);
    };
    logResult(authoritative ? spdlog::level::info : spdlog::level::debug);
    // Wait for the terminal result; never expose libtorrent-owned alert pointers.
    if (!authoritative)
        return;
    std::vector<DhtItemCallback> callbacks;
    {
        std::unique_lock lock(m_registryMutex);
        if (m_quarantine.erase(key))
            return;
        auto it = m_fetchCallbacks.find(key);
        if (it == m_fetchCallbacks.end())
            return;
        callbacks = std::move(it->second.callbacks);
        m_fetchCallbacks.erase(it);
    }
    DhtFetchResult result;
    result.sequence = sequence;
    result.requestId = requestId;
    result.callbackCount = callbacks.size();
    if (value.type() == lt::entry::string_t)
    {
        result.status = DhtFetchStatus::Found;
        result.value = value.string();
    }
    for (auto &callback : callbacks)
    {
        ++result.callbackIndex;
        try
        {
            callback(result);
        }
        catch (const std::exception &e)
        {
            SPDLOG_ERROR("[DHT] GET completion failed: {}", e.what());
        }
        catch (...)
        {
            SPDLOG_ERROR("[DHT] GET completion threw a non-standard exception");
        }
    }
}

lt::session *DhtManager::getSession() const
{
    return m_session.get();
}

void DhtManager::handleDhtPutAlert(const lt::dht_put_alert *alert)
{
    lt::hasher h(alert->public_key.data(), static_cast<int>(alert->public_key.size()));
    if (!alert->salt.empty())
    {
        h.update(alert->salt.data(), static_cast<int>(alert->salt.size()));
    }
    const lt::sha1_hash targetHash = h.final();
    std::string targetHex = Util::toHex(std::span<const char>(targetHash.data(), targetHash.size()));
    PublicationLogContext context{"<untracked>", "untracked"};
    {
        std::lock_guard lock(m_pendingPutsMutex);
        if (const auto it = m_publicationLogContexts.find(targetHex); it != m_publicationLogContexts.end())
            context = it->second;
    }
    // Handshake and anomaly publications share the rendezvous address.
    if (context.role == "handshake" || context.role == "anomaly")
        context.role = alert->seq == INT64_MAX ? "anomaly" : "handshake";
    SPDLOG_INFO("[DHT] Publication completed: {} | lobby={} target_sha1={} dht_seq={} successful_nodes={}",
                context.role, nlohmann::json(context.lobby).dump(), targetHex, alert->seq, alert->num_success);

    std::optional<PendingPut> pending;
    {
        std::unique_lock lock(m_pendingPutsMutex);
        auto it = m_pendingPuts.find(targetHex);
        if (it != m_pendingPuts.end())
        {
            // A stale response must not complete a newer publication.
            if (it->second.sequence > 0 && alert->seq != it->second.sequence)
                return;
            pending = std::move(it->second);
            m_pendingPuts.erase(it);
        }
    }
    if (pending)
    {
        if (pending->completion)
            pending->completion(alert->num_success > 0);
        if (alert->num_success == 0)
        {
            PutFailureCallback callback;
            {
                std::lock_guard lock(m_callbacksMutex);
                callback = m_onPutFailure;
            }
            if (callback)
                callback(pending->lobby, pending->sequence);
        }
    }
    if (alert->num_success == 0)
        recoverNetwork();
}

void DhtManager::handleDhtBootstrapAlert(const lt::dht_bootstrap_alert *alert)
{
    (void)alert;
    SPDLOG_INFO("[DHT] DHT bootstrap complete! The engine is connected to the network.");
    m_session->post_dht_stats();
    if (m_networkRecoveryPending.exchange(false, std::memory_order_acq_rel))
    {
        SPDLOG_INFO("[DHT] Network sockets successfully recovered via DHT bootstrap!");
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

void DhtManager::handleListenSucceededAlert(const lt::listen_succeeded_alert *alert)
{
    SPDLOG_INFO("[DHT] Network listening started successfully on port: {}", alert->port);
}

void DhtManager::handleListenFailedAlert(const lt::listen_failed_alert *alert)
{
    SPDLOG_ERROR("[DHT] Failed to open network port: {}", alert->error.message());
    KillSwitch::getInstance().handleListenFailed(alert->error.message());
}

void DhtManager::handleDhtErrorAlert(const lt::dht_error_alert *alert)
{
    SPDLOG_WARN("[DHT] Internal DHT error: {}", alert->error.message());
    recoverNetwork();
}

void DhtManager::onAlert(lt::alert *alert)
{
    if (auto *stats = lt::alert_cast<lt::dht_stats_alert>(alert))
        handleDhtStatsAlert(stats);
    else if (auto *putAlert = lt::alert_cast<lt::dht_put_alert>(alert))
        handleDhtPutAlert(putAlert);
    else if (auto *getMut = lt::alert_cast<lt::dht_mutable_item_alert>(alert))
        handleMutableItemAlert(getMut);
    else if (auto *bootstrap = lt::alert_cast<lt::dht_bootstrap_alert>(alert))
        handleDhtBootstrapAlert(bootstrap);
    else if (auto *listenOk = lt::alert_cast<lt::listen_succeeded_alert>(alert))
        handleListenSucceededAlert(listenOk);
    else if (auto *listenErr = lt::alert_cast<lt::listen_failed_alert>(alert))
        handleListenFailedAlert(listenErr);
    else if (auto *dhtErr = lt::alert_cast<lt::dht_error_alert>(alert))
        handleDhtErrorAlert(dhtErr);
}

void DhtManager::onTick()
{
    KillSwitch::getInstance().checkHealth();
    std::vector<DhtItemCallback> expired;
    const auto deadline = Clock::now();
    {
        std::unique_lock lock(m_registryMutex);
        std::erase_if(m_quarantine, [&](const auto &entry)
                      { return deadline >= entry.second; });
        for (auto it = m_fetchCallbacks.begin(); it != m_fetchCallbacks.end();)
        {
            if (deadline - it->second.createdAt < std::chrono::seconds(30))
            {
                ++it;
                continue;
            }
            SPDLOG_WARN("[DHT] DHT GET timeout request_id={} public_key_ed25519={} requested_seq={}",
                        it->second.reservation, it->first.substr(0, 64), it->second.request.sequence);
            for (auto &callback : it->second.callbacks)
                expired.push_back(std::move(callback));
            m_quarantine[it->first] = deadline + std::chrono::seconds(60);
            it = m_fetchCallbacks.erase(it);
        }
    }
    for (auto &callback : expired)
    {
        try
        {
            callback({DhtFetchStatus::Timeout, {}, 0});
        }
        catch (const std::exception &e)
        {
            SPDLOG_WARN("[DHT] Timed out GET completion: {}", e.what());
        }
        catch (...)
        {
            SPDLOG_WARN("[DHT] Timed out GET completion threw");
        }
    }
    std::vector<PutCompletion> puts;
    {
        std::unique_lock lock(m_pendingPutsMutex);
        for (auto it = m_pendingPuts.begin(); it != m_pendingPuts.end();)
        {
            if (deadline - it->second.createdAt < std::chrono::seconds(60))
            {
                ++it;
                continue;
            }
            SPDLOG_WARN("[DHT] DHT PUT timeout target_sha1={} seq={}", it->first, it->second.sequence);
            if (it->second.completion)
                puts.push_back(std::move(it->second.completion));
            it = m_pendingPuts.erase(it);
        }
    }
    for (auto &completion : puts)
    {
        try
        {
            completion(false);
        }
        catch (const std::exception &e)
        {
            SPDLOG_WARN("[DHT] Timed out PUT completion: {}", e.what());
        }
        catch (...)
        {
            SPDLOG_WARN("[DHT] Timed out PUT completion threw");
        }
    }

    auto now = std::chrono::steady_clock::now();
    if (m_session && std::chrono::duration_cast<std::chrono::seconds>(now - m_lastStatsTime).count() >= 5)
    {
        m_lastStatsTime = now;
        m_session->post_dht_stats();
    }
}

bool DhtManager::waitUntilDhtIsReady(std::chrono::seconds timeout, std::stop_token stop)
{
    std::unique_lock lock(m_readyMutex);
    m_readyCv.wait_for(lock, stop, timeout, [this]
                       { return m_isDhtReady.load() || !m_alive.load(); });
    return m_isDhtReady.load() && m_alive.load() && !stop.stop_requested();
}

std::optional<std::uint64_t> DhtManager::registerPendingPut(const std::string &targetHex, const std::string &lobbyName,
                                                            std::int64_t seqNum, PutCompletion completion)
{
    std::unique_lock lock(m_pendingPutsMutex);
    if (!m_alive.load() || m_pendingPuts.size() >= 256 || m_pendingPuts.contains(targetHex))
        return std::nullopt;
    const auto reservation = ++m_nextPutReservation;
    m_pendingPuts.emplace(targetHex, PendingPut{reservation, lobbyName, seqNum, std::move(completion)});
    return reservation;
}

bool DhtManager::hasSession() const
{
    return m_session != nullptr;
}

bool DhtManager::fetchDhtItem(const std::string &rawKeyString, const std::string &lobbyName, DhtItemCallback callback, DhtRequest request)
{
    if (!m_session || !callback || !m_alive.load())
        return false;

    const auto info = network::DhtAddress::keys(rawKeyString, lobbyName);
    const std::string opKey = info.pubKeyHex + ":" + info.salt;

    auto now = std::chrono::steady_clock::now();
    std::uint64_t reservation;
    {
        std::unique_lock lock(m_registryMutex);

        if (!m_alive.load() || m_quarantine.contains(opKey))
            return false;
        if (auto it = m_fetchCallbacks.find(opKey); it != m_fetchCallbacks.end())
        {
            if (it->second.callbacks.size() >= 8)
                return false;
            it->second.callbacks.push_back(std::move(callback));
            SPDLOG_DEBUG("[DHT] Lookup joined | lobby={} target_sha1={} request_id={} callbacks={} | Reusing the active lookup; no new network request",
                         nlohmann::json(lobbyName).dump(), info.targetIDHex, it->second.reservation, it->second.callbacks.size());
            return true;
        }
        if (m_fetchCallbacks.size() >= 128)
            return false;
        const auto active = std::count_if(m_fetchCallbacks.begin(), m_fetchCallbacks.end(), [&](const auto &pair)
                                          { return pair.second.lobby == lobbyName && pair.second.request.kind == request.kind; });
        const auto limit = request.kind == DhtFetchKind::Message ? m_fetchLimit.load() : std::size_t{8};
        if (static_cast<std::size_t>(active) >= limit)
            return false;
        reservation = ++m_nextGetReservation;
        m_fetchCallbacks.emplace(opKey, FetchContext{reservation, {std::move(callback)}, lobbyName, request, now});
    }
    try
    {
        const std::string role = !request.logRole.empty() ? request.logRole :
                                 request.kind == DhtFetchKind::Handshake ? "handshake" :
                                 request.kind == DhtFetchKind::Sequence ? "sequence_announcement" :
                                 request.kind == DhtFetchKind::Message ? "message" : "signaling";
        SPDLOG_INFO("[DHT] Fetch {} | lobby={} target_sha1={} request_id={} message_seq={}",
                    role, nlohmann::json(lobbyName).dump(), info.targetIDHex, reservation, request.sequence < 0 ? "none" : std::to_string(request.sequence));

        m_session->dht_get_item(info.publicKey, info.salt);
    }
    catch (...)
    {
        cancelPendingGet(opKey, reservation);
        throw;
    }
    return true;
}
void DhtManager::rememberPublicationLog(const std::string &target, const std::string &lobby, const std::string &role)
{
    std::lock_guard lock(m_pendingPutsMutex);
    if (m_publicationLogContexts.size() >= 1024 && !m_publicationLogContexts.contains(target))
        m_publicationLogContexts.erase(m_publicationLogContexts.begin());
    m_publicationLogContexts[target] = {lobby, role};
}

bool DhtManager::publishHandshake(const std::string &lobby, const std::string &payload,
                                  std::int64_t sequence, PutCompletion completion)
{
    if (!m_alive.load())
        return false;
    auto keys = network::DhtAddress::keys(lobby, lobby);
    rememberPublicationLog(keys.targetIDHex, lobby, "handshake");
    Dropper dropper(getSession(), keys);
    return dropper.dropHandShakeMessage(payload, lobby, sequence, this, std::move(completion));
}

bool DhtManager::publishChatMessage(const ChatPublishParams &params, PutCompletion completion)
{
    if (!m_alive.load())
        return false;
    auto keys = network::DhtAddress::keys(params.messageTargetKey, params.lobbyName);
    rememberPublicationLog(keys.targetIDHex, params.lobbyName, "message");
    Dropper dropper(getSession(), keys);
    return dropper.dropChatMessage(params, this, std::move(completion));
}

bool DhtManager::publishSequenceAnnouncement(const ChatPublishParams &params)
{
    if (!m_alive.load())
        return false;
    auto keys = network::DhtAddress::keys(params.sequenceTargetKey, params.lobbyName);
    rememberPublicationLog(keys.targetIDHex, params.lobbyName, "sequence_announcement");
    Dropper dropper(getSession(), keys);
    return dropper.dropSequenceAnnouncement(params);
}

bool DhtManager::publishAnomaly(const std::string &lobby, const std::string &reason)
{
    if (!m_alive.load())
        return false;
    auto keys = network::DhtAddress::keys(lobby, lobby);
    rememberPublicationLog(keys.targetIDHex, lobby, "anomaly");
    Dropper dropper(getSession(), keys);
    return dropper.dropAnomalyMessage(reason, lobby);
}

bool DhtManager::publishHeartbeat(int offsetSeconds, const std::string &lobby, const std::string &key)
{
    if (!m_alive.load())
        return false;
    auto keys = network::DhtAddress::keys(key, lobby);
    rememberPublicationLog(keys.targetIDHex, lobby, "heartbeat");
    Dropper dropper(getSession(), keys);
    return dropper.dropHeartbeatMessage(offsetSeconds, lobby);
}

bool DhtManager::publishAck(const std::string &message, const std::string &lobby, const std::string &key, int offsetSeconds)
{
    if (!m_alive.load())
        return false;
    auto keys = network::DhtAddress::keys(key, lobby);
    rememberPublicationLog(keys.targetIDHex, lobby, "ack");
    Dropper dropper(getSession(), keys);
    return dropper.dropAckMessage(message, lobby, offsetSeconds);
}

void DhtManager::cancelPendingGet(const std::string &key, std::uint64_t reservation)
{
    std::vector<DhtItemCallback> callbacks;
    {
        std::lock_guard lock(m_registryMutex);
        auto it = m_fetchCallbacks.find(key);
        if (it == m_fetchCallbacks.end() || it->second.reservation != reservation)
            return;
        callbacks = std::move(it->second.callbacks);
        m_fetchCallbacks.erase(it);
    }
    for (auto &callback : callbacks)
    {
        try
        {
            callback({DhtFetchStatus::Cancelled, {}, 0});
        }
        catch (...)
        {
            SPDLOG_WARN("[DHT] GET rollback callback failed");
        }
    }
}

void DhtManager::cancelPendingPut(const std::string &key, std::uint64_t reservation)
{
    PutCompletion completion;
    {
        std::lock_guard lock(m_pendingPutsMutex);
        auto it = m_pendingPuts.find(key);
        if (it == m_pendingPuts.end() || it->second.reservation != reservation)
            return;
        completion = std::move(it->second.completion);
        m_pendingPuts.erase(it);
    }
    if (completion)
    {
        try
        {
            completion(false);
        }
        catch (...)
        {
            SPDLOG_WARN("[DHT] PUT rollback callback failed");
        }
    }
}

void DhtManager::requestStop()
{
    {
        std::lock_guard lock(m_readyMutex);
        m_alive.store(false, std::memory_order_release);
    }
    m_stopSource.request_stop();
    m_readyCv.notify_all();
}
