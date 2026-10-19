#include "LogEvents.h"
#include "MessageReceiver.h"
#include "DhtTransport.h"
#include "LobbyStore.h"
#include "PartitionedThreadPool.h"
#include "DhtMessageCodec.h"
#include <spdlog/spdlog.h>

bool MessageReceiver::fetchLatestSequence(std::shared_ptr<LobbyObserver> lobby)
{
    if (!lobby) return false;
    const auto data = lobby->getLobbyDataWithoutHistory();
    if (data.metadata.is_hidden || !data.crypto || data.crypto->private_node_discussion.empty()) return false;
    const auto name = data.metadata.lobby_name;
    const auto key = data.crypto->private_node_discussion + data.crypto->announced_x25519_pub;
    { std::lock_guard lock(m_mutex); if (!m_pendingSequences.insert(name).second) return false; }
    try {
        const bool accepted = m_manager.fetchDhtItem(key, name,
            [this, lobby, name](DhtFetchResult result) {
                { std::lock_guard lock(m_mutex); m_pendingSequences.erase(name); }
                if (result.status == DhtFetchStatus::Cancelled || result.status == DhtFetchStatus::Timeout) return;
                m_executor.post(name, [this, lobby, result = std::move(result)] { receiveSequence(lobby, result); });
            }, {DhtFetchKind::Sequence, -1});
        if (!accepted) { std::lock_guard lock(m_mutex); m_pendingSequences.erase(name); }
        return accepted;
    } catch (...) {
        std::lock_guard lock(m_mutex);
        m_pendingSequences.erase(name);
        throw;
    }
}

bool MessageReceiver::fetchMessage(const std::string& key, std::shared_ptr<LobbyObserver> lobby,
                                  std::int64_t sequence)
{
    const auto name = lobby->getLobbyName();
    return m_manager.fetchDhtItem(key, name,
        [this, lobby, name, sequence](DhtFetchResult result) {
            if (result.status == DhtFetchStatus::Cancelled || result.status == DhtFetchStatus::Timeout) return;
            m_executor.post(name, [this, lobby, sequence, result = std::move(result)] {
                receiveMessage(lobby, sequence, result);
            });
        }, {DhtFetchKind::Message, sequence});
}

void MessageReceiver::receiveSequence(std::shared_ptr<LobbyObserver> lobby, const DhtFetchResult& result)
{
    const auto data = lobby->getLobbyDataWithoutHistory();
    if (data.metadata.is_hidden || !result.found()) return;
    if (result.sequence > data.sync.announced_seq_num) {
        m_store.execute(data.metadata.lobby_name, lobby::AnnounceSequence{result.sequence});
        SPDLOG_INFO("[MSG-IN] Discovered new messages for lobby '{}'. Target seq: {}", nlohmann::json(data.metadata.lobby_name).dump(), result.sequence);
    }
    fetchMissingMessages(result.sequence, data, std::move(lobby));
}

void MessageReceiver::receiveMessage(std::shared_ptr<LobbyObserver> lobby, std::int64_t sequence,
                                     const DhtFetchResult& result)
{
    const auto data = lobby->getLobbyDataWithoutHistory();
    if (data.metadata.is_hidden) return;
    if (!result.found() || result.value.empty()) {
        SPDLOG_WARN("[MSG-IN] lobby={} seq={} | Message unavailable; retry on next cycle", nlohmann::json(data.metadata.lobby_name).dump(), sequence);
        return;
    }
    auto message = network::DhtMessageCodec::parseChatMessage(result.value, sequence);
    if (!message) { SPDLOG_WARN("[MSG-IN] lobby={} seq={} | Invalid message rejected", nlohmann::json(data.metadata.lobby_name).dump(), sequence); return; }
    if (data.sync.downloaded_seq_nums.acknowledges(static_cast<std::uint64_t>(sequence))) {
        SPDLOG_INFO("[MSG-IN] lobby={} seq={} | Message ignored: already received", nlohmann::json(data.metadata.lobby_name).dump(), sequence);
        return;
    }
    if (!network::DhtMessageCodec::isTimestampValid(message->fake_timestamp)) {
        const std::string reason = "Corrupted timestamp received from peer";
        m_store.execute(data.metadata.lobby_name, lobby::FlagAnomaly{reason});
        m_manager.publishAnomaly(data.metadata.lobby_name, reason);
        return;
    }
    if (m_store.execute(data.metadata.lobby_name, lobby::ReceiveMessage{std::move(*message)})) {
        SPDLOG_INFO("[MSG-IN] lobby={} seq={} | Message accepted and synchronized with journal", nlohmann::json(data.metadata.lobby_name).dump(), sequence);
    } else if (lobby->getNetworkState().downloaded_seq_nums.acknowledges(static_cast<std::uint64_t>(sequence))) {
        SPDLOG_INFO("[MSG-IN] lobby={} seq={} | Message ignored: already received", nlohmann::json(data.metadata.lobby_name).dump(), sequence);
    }
}

void MessageReceiver::fetchMissingMessages(std::int64_t announced, const LobbyData& data,
                                           std::shared_ptr<LobbyObserver> lobby)
{
    if (!data.crypto || data.crypto->private_node_discussion.empty()) return;
    const auto now = ReceivePlanner::Clock::now();
    std::vector<std::int64_t> requested;
    {
        std::lock_guard lock(m_mutex);
        requested = m_receivePlanners[data.metadata.lobby_name].plan(announced, data.sync.downloaded_seq_nums, m_fetchBudget, now);
    }
    for (const auto sequence : requested) {
        const auto key = data.crypto->private_node_discussion + data.crypto->announced_x25519_pub + std::to_string(sequence);
        if (fetchMessage(key, lobby, sequence)) {
            std::lock_guard lock(m_mutex);
            m_receivePlanners[data.metadata.lobby_name].accepted(sequence, now);
        }
    }
}
