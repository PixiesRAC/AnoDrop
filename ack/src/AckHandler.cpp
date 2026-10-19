#include "LogEvents.h"
#include "AckHandler.h"

#include <spdlog/spdlog.h>

#include "SecureBuffer.h"

AckHandler::AckHandler(std::shared_ptr<DhtPublisher> dhtPublisher)
    : m_dhtPublisher(std::move(dhtPublisher))
{
}

void AckHandler::pushAcks(const LobbyData &lobbyData)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager() || !lobbyData.crypto || lobbyData.crypto->private_node_discussion.empty())
        return;

    std::string lobbyName = lobbyData.metadata.lobby_name;
    std::string key = lobbyData.crypto->private_node_discussion + lobbyData.crypto->my_x25519_pub + ACK_KEY;
    ScopedCleanse keyGuard(key);

    const auto &receivedSeqs = lobbyData.sync.downloaded_seq_nums;
    if (receivedSeqs.through == 0 && receivedSeqs.selective.empty())
        return;

    // Compression only reads this snapshot; the cache mutex is not needed.
    const std::string newCompressed = compressAcks(receivedSeqs);
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(m_cache->ackMutex);
        const auto previous = m_cache->lastPushedAckString.find(lobbyName);
        const auto lastPush = m_cache->lastAckPushTime.find(lobbyName);
        if (previous != m_cache->lastPushedAckString.end() && previous->second == newCompressed &&
            lastPush != m_cache->lastAckPushTime.end() && now - lastPush->second < std::chrono::seconds(60))
            return;
    }
    const auto publishedReceipts = parseAcks(newCompressed);
    SPDLOG_INFO("[ACK] lobby={} ack_through={} ack_selective={} | Publishing receipts for incoming messages (periodic refresh may repeat the same ACK)",
                nlohmann::json(lobbyName).dump(), publishedReceipts.through, nlohmann::json(publishedReceipts.selective).dump());

    // Only durable local receipts may be acknowledged. A value read back from
    // the DHT is not evidence that the corresponding body is stored locally.
    if (!m_dhtPublisher->getDhtManager()->publishAck(newCompressed, lobbyName, key,
                                                     lobbyData.metadata.fake_timezone.offset_seconds))
        return;
    {
        std::lock_guard cacheLock(m_cache->ackMutex);
        m_cache->lastPushedAckString[lobbyName] = newCompressed;
        m_cache->lastAckPushTime[lobbyName] = now;
    }
}

void AckHandler::fetchPeerAcks(const LobbyData &lobbyData, std::shared_ptr<LobbyStore> lobbyStore)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager() || !lobbyStore || !lobbyData.crypto || lobbyData.crypto->private_node_discussion.empty())
        return;

    std::string lobbyName = lobbyData.metadata.lobby_name;
    std::string peerAckKey = lobbyData.crypto->private_node_discussion + lobbyData.crypto->announced_x25519_pub + ACK_KEY;
    ScopedCleanse peerKeyGuard(peerAckKey);

    std::weak_ptr<DhtPublisher> weakPublisher = m_dhtPublisher;
    std::weak_ptr<CacheState> weakCache = m_cache;
    auto callback = [lobbyName, lobbyStore, weakPublisher, weakCache](DhtFetchResult result)
    {
        if (result.status == DhtFetchStatus::Timeout || result.status == DhtFetchStatus::Cancelled)
            return;
        if (!result.found())
            return;
        if (result.value.empty()) {
            SPDLOG_INFO("[ACK] lobby={} request_id={} callback={}/{} dht_seq={} | ACK ignored: empty value; no journal write",
                        nlohmann::json(lobbyName).dump(), result.requestId, result.callbackIndex, result.callbackCount, result.sequence);
            return;
        }
        auto valueStr = std::move(result.value);
        result.value.clear();

        if (auto pub = weakPublisher.lock())
        {
            pub->enqueueNetworkTask(lobbyName, [lobbyName, lobbyStore, valueStr = std::move(valueStr), weakCache, result]()
                                    {
                auto cache = weakCache.lock();
                if (!cache) return;

                const auto logReceipt = [&](const AckState &receipts, const char *outcome) {
                    SPDLOG_INFO("[ACK] lobby={} request_id={} callback={}/{} dht_seq={} ack_through={} ack_selective={} | {}",
                                nlohmann::json(lobbyName).dump(), result.requestId, result.callbackIndex, result.callbackCount,
                                result.sequence, receipts.through, nlohmann::json(receipts.selective).dump(), outcome);
                };
                {
                    std::lock_guard<std::mutex> cb_lock(cache->ackMutex);
                    if (const auto it = cache->lastFetchedPeerAckString.find(lobbyName);
                        it != cache->lastFetchedPeerAckString.end() && it->second.encoded == valueStr)
                    {
                        logReceipt(it->second.receipts, "ACK ignored: already received; no journal write");
                        return;
                    }
                }

                try
                {
                    AckState ackedSeqs = parseAcks(valueStr);
                    if (ackedSeqs.through > 0 || !ackedSeqs.selective.empty())
                    {
                        auto obs = lobbyStore->getLobby(lobbyName);
                        if (obs)
                        {
                            const bool changed = lobbyStore->execute(lobbyName, lobby::RecordPeerReceipts{ackedSeqs});
                            {
                                std::lock_guard cb_lock(cache->ackMutex);
                                cache->lastFetchedPeerAckString[lobbyName] = {valueStr, ackedSeqs};
                            }
                            logReceipt(ackedSeqs, changed
                                ? "ACK accepted and synchronized with journal; outgoing messages acknowledged"
                                : "ACK ignored: already recorded in journal; no journal write");
                        } else {
                            logReceipt(ackedSeqs, "ACK ignored: lobby no longer available; no journal write");
                        }
                    } else {
                        logReceipt(ackedSeqs, "ACK ignored: no messages acknowledged; no journal write");
                    }
                }
                catch (const std::invalid_argument &) {
                    SPDLOG_WARN("[ACK] lobby={} request_id={} dht_seq={} | ACK rejected: malformed receipts or acknowledgement exceeds sent messages; no journal write",
                                nlohmann::json(lobbyName).dump(), result.requestId, result.sequence);
                }
                catch (const std::exception &e)
                {
                    SPDLOG_ERROR("[ACK] lobby={} request_id={} dht_seq={} | ACK processing failed; journal synchronization not confirmed", nlohmann::json(lobbyName).dump(), result.requestId, result.sequence);
                } });
        }
    };

    m_dhtPublisher->getDhtManager()->fetchDhtItem(peerAckKey, lobbyName, callback, {DhtFetchKind::Signaling, -1, "ack"});
}
