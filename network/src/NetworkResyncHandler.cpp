#include "NetworkResyncHandler.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "DhtPublisher.h"
#include "DhtManager.h"
#include "LobbyObserver.h"
#include "LobbyStore.h"
#include "LobbyDataSerializer.h"
#include "DhtChatUtils.h"

NetworkResyncHandler::NetworkResyncHandler(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore)
    : m_dhtPublisher(std::move(dhtPublisher)), m_lobbyStore(std::move(lobbyStore))
{
}

NetworkResyncHandler::~NetworkResyncHandler()
{
    detach();
}

void NetworkResyncHandler::detach()
{
    m_alive->store(false, std::memory_order_release);
    auto token = m_dhtManagerAlive.lock();
    if (token && token->load(std::memory_order_acquire) && m_attachedDhtManager)
    {
        m_attachedDhtManager->setPutFailureCallback(nullptr);
        m_attachedDhtManager->setNetworkRecoveredCallback(nullptr);
    }
    m_attachedDhtManager = nullptr;
    std::unique_lock<std::shared_mutex> lock(m_callbackMutex);
}

void NetworkResyncHandler::attach(DhtManager *dhtManager)
{
    if (!dhtManager)
    {
        return;
    }

    m_attachedDhtManager = dhtManager;
    m_dhtManagerAlive = dhtManager->getAliveToken();

    dhtManager->setPutFailureCallback([alive = m_alive, this](const std::string &lobbyName, std::int64_t seqNum) {
        if (!alive->load(std::memory_order_acquire))
            return;
        std::shared_lock<std::shared_mutex> lock(m_callbackMutex);
        if (!alive->load(std::memory_order_acquire))
            return;
        this->onPutFailed(lobbyName, seqNum);
    });

    dhtManager->setNetworkRecoveredCallback([alive = m_alive, this]() {
        if (!alive->load(std::memory_order_acquire))
            return;
        std::shared_lock<std::shared_mutex> lock(m_callbackMutex);
        if (!alive->load(std::memory_order_acquire))
            return;
        this->onNetworkRecovered();
    });

    SPDLOG_INFO("NetworkResyncHandler attached to DhtManager successfully");
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
    SPDLOG_INFO("NetworkResyncHandler: Network recovered, triggering resync for unconfirmed messages");
    retryAllUnconfirmedMessages();
}

void NetworkResyncHandler::retryUnconfirmedMessages(std::shared_ptr<LobbyObserver> lobbyObs)
{
    if (!lobbyObs)
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
    if (data.is_hidden || (data.status != LobbyStatus::HANDSHAKE_DONE && data.status != LobbyStatus::ANOMALY))
    {
        return;
    }

    lobbyObs->withHistory([&](std::span<const ChatMessage> messages) {
        auto now = std::chrono::steady_clock::now();
        size_t enqueuedCount = 0;
        constexpr size_t MAX_RESYNC_BATCH = 10;

        for (auto it = messages.rbegin(); it != messages.rend(); ++it)
        {
            if (enqueuedCount >= MAX_RESYNC_BATCH)
            {
                break;
            }

            const auto &msg = *it;
            if (msg.direction != MessageDirection::OUT)
            {
                continue;
            }

            if (msg.seq_num > 0 && static_cast<std::uint64_t>(msg.seq_num) <= data.peer_acked_seq_nums.through)
            {
                break; // Guaranteed acknowledged by peer along with all older messages
            }

            bool isSending = !data.dht_acked_seq_nums.acknowledges(msg.seq_num);
            bool isUnacked = !data.peer_acked_seq_nums.acknowledges(msg.seq_num);

            if (!isSending && !isUnacked)
            {
                continue;
            }

            bool isResynced = std::binary_search(
                data.resynced_seq_nums.begin(),
                data.resynced_seq_nums.end(),
                msg.seq_num);
            if (isResynced)
            {
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(m_retryMutex);
                auto &lobbyRetries = m_outboxRetryAttempts[data.lobby_name];
                auto &tracker = lobbyRetries[msg.seq_num];

                // Exponential backoff: baseCooldown * 2^(min(attemptCount, 4))
                int baseCooldown = isSending ? 10 : 30;
                int backoffMultiplier = 1 << std::min(tracker.attemptCount, 4); // 1x, 2x, 4x, 8x, 16x
                int cooldownSec = std::min(baseCooldown * backoffMultiplier, 300); // Max 5 minutes

                if (tracker.attemptCount > 0 &&
                    std::chrono::duration_cast<std::chrono::seconds>(now - tracker.lastAttempt).count() < cooldownSec)
                {
                    continue;
                }

                tracker.lastAttempt = now;
                tracker.attemptCount++;
            }

            SPDLOG_INFO("NetworkResyncHandler: Resending message SeqNum {} in lobby '{}' (status: {})",
                        msg.seq_num, data.lobby_name, isSending ? "Sending" : "Unacknowledged");

            nlohmann::json msgJson = msg;
            std::string payloadStr = msgJson.dump();

            ChatPublishParams params;
            params.encryptedPayload = payloadStr;
            params.lobbyName = data.lobby_name;
            params.targetSeqNum = msg.seq_num;
            params.sequenceTargetKey = data.private_node_discussion + data.my_x25519_pub;
            params.announcedTargetKey = data.private_node_discussion + data.announced_x25519_pub;
            params.messageTargetKey = data.private_node_discussion + data.my_x25519_pub + std::to_string(msg.seq_num);

            publisher->enqueueChatMessage(params);
            ++enqueuedCount;
        }

        // Cleanup acknowledged message trackers to prevent memory growth
        {
            std::lock_guard<std::mutex> lock(m_retryMutex);
            auto &lobbyRetries = m_outboxRetryAttempts[data.lobby_name];
            std::erase_if(lobbyRetries, [&](const auto &pair) {
                return (pair.first > 0 && static_cast<std::uint64_t>(pair.first) <= data.peer_acked_seq_nums.through)
                    || data.peer_acked_seq_nums.acknowledges(static_cast<std::uint64_t>(pair.first));
            });
        }
    });
}

void NetworkResyncHandler::retryAllUnconfirmedMessages()
{
    std::shared_ptr<LobbyStore> store = m_lobbyStore;
    if (!store)
    {
        auto publisher = m_dhtPublisher.lock();
        if (publisher && publisher->getDhtManager())
        {
            store = publisher->getDhtManager()->getLobbyStore();
        }
    }

    if (!store)
    {
        return;
    }

    auto lobbies = store->getLobbies();
    for (const auto &lobbyObs : lobbies)
    {
        retryUnconfirmedMessages(lobbyObs);
    }
}
