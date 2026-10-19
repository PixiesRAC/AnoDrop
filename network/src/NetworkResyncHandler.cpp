#include "LogEvents.h"
#include "NetworkResyncHandler.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "DhtChatUtils.h"
#include "DhtManager.h"
#include "DhtPublisher.h"
#include "LobbyDataSerializer.h"
#include "LobbyObserver.h"
#include "LobbyStore.h"
#include "TimeUtils.h"

NetworkResyncHandler::NetworkResyncHandler(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore)
    : m_dhtPublisher(std::move(dhtPublisher)), m_lobbyStore(std::move(lobbyStore))
{
    if (m_dhtPublisher.expired() || !m_lobbyStore)
        throw std::invalid_argument("NetworkResyncHandler requires publisher and store");
}

NetworkResyncHandler::~NetworkResyncHandler()
{
    detach();
}

void NetworkResyncHandler::detach()
{
    m_alive.store(false, std::memory_order_release);
    ++m_callbackGeneration;
}

void NetworkResyncHandler::attach(DhtManager *dhtManager)
{
    if (!dhtManager)
    {
        return;
    }

    const auto generation = ++m_callbackGeneration;

    m_alive.store(true, std::memory_order_release);
    const std::weak_ptr<NetworkResyncHandler> weak = shared_from_this();
    dhtManager->setPutFailureCallback([weak, generation](const std::string &lobbyName, std::int64_t seqNum)
                                      {
        if (auto self = weak.lock(); self && self->m_alive.load(std::memory_order_acquire) && self->m_callbackGeneration.load() == generation)
            self->onPutFailed(lobbyName, seqNum); });
    dhtManager->setNetworkRecoveredCallback([weak, generation]()
                                            {
        if (auto self = weak.lock(); self && self->m_alive.load(std::memory_order_acquire) && self->m_callbackGeneration.load() == generation)
            self->onNetworkRecovered(); });

    SPDLOG_INFO("[RESYNC] NetworkResyncHandler attached to DhtManager successfully");
}

void NetworkResyncHandler::onPutFailed(const std::string &lobbyName, std::int64_t seqNum)
{
    std::lock_guard<std::mutex> lock(m_retryMutex);
    auto itLobby = m_outboxRetryAttempts.find(lobbyName);
    if (itLobby != m_outboxRetryAttempts.end())
    {
        itLobby->second.erase(seqNum);
    }
}

void NetworkResyncHandler::onNetworkRecovered()
{
    SPDLOG_INFO("[RESYNC] NetworkResyncHandler: Network recovered, triggering resync for unconfirmed messages");
    if (auto publisher = m_dhtPublisher.lock())
    {
        auto self = shared_from_this();
        publisher->enqueuePollingTask("", "network-recovery", [self]
                                      { self->retryAllUnconfirmedMessages(); });
    }
}

void NetworkResyncHandler::retryUnconfirmedMessages(std::shared_ptr<LobbyObserver> lobbyObs)
{
    if (!lobbyObs || !m_alive.load(std::memory_order_acquire))
    {
        return;
    }

    if (lobbyObs->getMetadata().is_hidden)
    {
        return;
    }

    auto publisher = m_dhtPublisher.lock();
    if (!publisher)
    {
        return;
    }

    LobbyData data = lobbyObs->getLobbyDataWithoutHistory();
    if (data.metadata.is_hidden || (data.metadata.status != LobbyStatus::HANDSHAKE_DONE && data.metadata.status != LobbyStatus::ANOMALY)
        || !data.crypto || data.crypto->private_node_discussion.empty())
    {
        return;
    }

    std::int64_t cursor;
    {
        std::lock_guard lock(m_retryMutex);
        cursor = m_outboxCursor[data.metadata.lobby_name];
    }
    auto candidateSeqs = lobbyObs->pendingOutgoingSequences(cursor, 10);
    if (candidateSeqs.empty() && cursor != 0)
        candidateSeqs = lobbyObs->pendingOutgoingSequences(0, 10);
    {
        std::lock_guard lock(m_retryMutex);
        m_outboxCursor[data.metadata.lobby_name] = candidateSeqs.empty() ? 0 : candidateSeqs.back();
    }
    {
        auto now = std::chrono::steady_clock::now();
        size_t enqueuedCount = 0;
        constexpr size_t MAX_RESYNC_BATCH = 10;

        for (auto it = candidateSeqs.begin(); it != candidateSeqs.end(); ++it)
        {
            if (enqueuedCount >= MAX_RESYNC_BATCH)
            {
                break;
            }

            std::int64_t seqNum = *it;

            if (seqNum > 0 && static_cast<std::uint64_t>(seqNum) <= data.sync.peer_acked_seq_nums.through)
            {
                break; // Guaranteed acknowledged by peer along with all older messages
            }

            bool isSending = !data.sync.dht_acked_seq_nums.acknowledges(seqNum);
            bool isUnacked = !data.sync.peer_acked_seq_nums.acknowledges(seqNum);

            if (!isSending && !isUnacked)
            {
                continue;
            }

            bool isResynced = std::binary_search(
                data.sync.resynced_seq_nums.begin(),
                data.sync.resynced_seq_nums.end(),
                seqNum);
            if (isResynced)
            {
                continue;
            }

            RetryTracker previous;
            std::uint64_t reservation;
            {
                std::lock_guard<std::mutex> lock(m_retryMutex);
                auto &lobbyRetries = m_outboxRetryAttempts[data.metadata.lobby_name];
                auto &tracker = lobbyRetries[seqNum];

                // Exponential backoff: baseCooldown * 2^(min(attemptCount, 4))
                int baseCooldown = isSending ? 10 : 30;
                int backoffMultiplier = 1 << std::min(tracker.attemptCount, 4);    // 1x, 2x, 4x, 8x, 16x
                int cooldownSec = std::min(baseCooldown * backoffMultiplier, static_cast<int>(TimeUtils::MAX_RETRY_COOLDOWN_SECONDS));

                if (tracker.attemptCount > 0 &&
                    std::chrono::duration_cast<std::chrono::seconds>(now - tracker.lastAttempt).count() < cooldownSec)
                {
                    continue;
                }

                previous = tracker;
                reservation = ++m_nextReservation;
                tracker.reservation = reservation;
                tracker.lastAttempt = now;
                tracker.attemptCount = std::min(tracker.attemptCount + 1, 4);
            }

            // Only load and decrypt the message now that cooldown has elapsed and we are committing to resend
            auto optMsg = lobbyObs->getOutgoingMessage(seqNum);
            if (!optMsg || optMsg->direction != MessageDirection::OUT)
            {
                std::lock_guard lock(m_retryMutex);
                auto &retries = m_outboxRetryAttempts.at(data.metadata.lobby_name);
                auto retry = retries.find(seqNum);
                if (retry != retries.end() && retry->second.reservation == reservation)
                    retry->second = previous;
                continue;
            }

            const auto &msg = *optMsg;
            nlohmann::json msgJson = msg;
            std::string payloadStr = msgJson.dump();

            ChatPublishParams params;
            params.payload = payloadStr;
            params.lobbyName = data.metadata.lobby_name;
            params.targetSeqNum = msg.seq_num;
            params.sequenceTargetKey = data.crypto->private_node_discussion + data.crypto->my_x25519_pub;
            params.messageTargetKey = data.crypto->private_node_discussion + data.crypto->my_x25519_pub + std::to_string(msg.seq_num);

            try
            {
                SPDLOG_INFO("[RESYNC] lobby={} seq={} status={} | Resending unconfirmed message",
                            LogEvents::lobbyId(data.metadata.lobby_name), msg.seq_num, isSending ? "Sending" : "Unacknowledged");

                publisher->enqueueChatMessage(params);
            }
            catch (...)
            {
                std::lock_guard lock(m_retryMutex);
                auto &retries = m_outboxRetryAttempts.at(data.metadata.lobby_name);
                auto retry = retries.find(seqNum);
                if (retry != retries.end() && retry->second.reservation == reservation)
                    retry->second = previous;
                throw;
            }
            ++enqueuedCount;
        }

        // Cleanup acknowledged message trackers to prevent memory growth
        {
            std::lock_guard<std::mutex> lock(m_retryMutex);
            auto &lobbyRetries = m_outboxRetryAttempts[data.metadata.lobby_name];
            std::erase_if(lobbyRetries, [&](const auto &pair)
                          { return (pair.first > 0 && static_cast<std::uint64_t>(pair.first) <= data.sync.peer_acked_seq_nums.through) || data.sync.peer_acked_seq_nums.acknowledges(static_cast<std::uint64_t>(pair.first)); });
        }
    }
}

void NetworkResyncHandler::retryAllUnconfirmedMessages()
{
    auto lobbies = m_lobbyStore->getLobbies();
    for (const auto &lobbyObs : lobbies)
    {
        if (auto pub = m_dhtPublisher.lock())
        {
            auto self = shared_from_this();
            pub->enqueuePollingTask(lobbyObs->getLobbyName(), "outbox", [self, lobbyObs]
                                    { self->retryUnconfirmedMessages(lobbyObs); });
        }
    }
}
