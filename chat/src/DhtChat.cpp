#include "LogEvents.h"
#include "DhtChat.h"

#include <spdlog/spdlog.h>

#include "DhtChatUtils.h"
#include "DhtMessageCodec.h"
#include "LobbyStore.h"

DhtChat::DhtChat(std::shared_ptr<LobbyObserver> lobby, std::shared_ptr<OutgoingMessageQueue> publisher,
                 std::shared_ptr<LobbyStore> store)
    : m_lobby(std::move(lobby)), m_publisher(std::move(publisher)), m_store(std::move(store))
{
    if (!m_lobby || !m_publisher || !m_store)
        throw std::invalid_argument("DhtChat requires a lobby, publisher and store");
}

std::expected<std::int64_t, SendError> DhtChat::sendMessage(const std::string &message)
{
    std::expected<std::int64_t, SendError> result = std::unexpected(SendError::StorageFailed);
    SPDLOG_INFO("[USER] lobby={} | Send requested", LogEvents::lobbyId(getLobbyName()));
    ChatPublishParams params;
    {
        std::unique_lock sendLock(m_sendMutex);
        const auto now = std::chrono::steady_clock::now();
        while (!m_recentSends.empty() && now - m_recentSends.front() >= std::chrono::seconds(RATE_LIMIT_WINDOW_SECONDS))
            m_recentSends.pop_front();
        if (m_recentSends.size() >= RATE_LIMIT_MAX_MESSAGES) {
            SPDLOG_WARN("[MSG-OUT] lobby={} | Send rejected: rate limit", LogEvents::lobbyId(getLobbyName()));
            return std::unexpected(SendError::RateLimited);
        }
        bool committed = false;
        // Reserve before persistence so allocation failure cannot disguise a committed send.
        m_recentSends.push_back(now);
        struct Reservation
        {
            std::deque<std::chrono::steady_clock::time_point> &sends;
            bool &committed;
            ~Reservation()
            {
                if (!committed)
                    sends.pop_back();
            }
        } reservation{m_recentSends, committed};
        try
        {
            m_store->transact(getLobbyName(), [&](const LobbyData &state)
                              {
            LobbyUpdate update;
            if (state.sync.my_seq_num == INT64_MAX) {
                result = std::unexpected(SendError::SequenceExhausted); return update;
            }
            if (state.metadata.is_hidden || (state.metadata.status != LobbyStatus::HANDSHAKE_DONE && state.metadata.status != LobbyStatus::ANOMALY)
                || !state.crypto || state.crypto->private_node_discussion.empty()) {
                result = std::unexpected(SendError::Unavailable); return update;
            }
            ChatMessage msg{MessageDirection::OUT, message,
                TimeUtils::computeFakeTimestamp(state.metadata.fake_timezone.offset_seconds), state.sync.my_seq_num + 1};
            const auto payload = nlohmann::json(msg).dump();
            if (!network::DhtMessageCodec::fitsBep44Sealed(payload.size())) {
                result = std::unexpected(SendError::TooLarge); return update;
            }
            params.payload = payload;
            params.lobbyName = state.metadata.lobby_name;
            params.targetSeqNum = msg.seq_num;
            params.sequenceTargetKey = state.crypto->private_node_discussion + state.crypto->my_x25519_pub;
            params.messageTargetKey = params.sequenceTargetKey + std::to_string(msg.seq_num);
            params.recipientX25519Pub = state.crypto->announced_x25519_pub;
            update = lobby::prepare(state, lobby::StoreOutgoingMessage{msg});
            result = msg.seq_num;
            return update; });
        }
        catch (const StoreRecoveryRequired &)
        {
            return std::unexpected(SendError::RecoveryRequired);
        }
        catch (const std::exception &e)
        {
            SPDLOG_ERROR("[MSG-OUT] Cannot persist outgoing message: {}", e.what());
            return std::unexpected(SendError::StorageFailed);
        }
        committed = result.has_value();
    } // Reservation and send mutex are released before calling the queue.
    if (result)
    {
        SPDLOG_DEBUG("[MSG-OUT] lobby={} seq={} | Message queued for publication", LogEvents::lobbyId(getLobbyName()), *result);
        try
        {
            m_publisher->enqueueChatMessage(params);
        }
        catch (const std::exception &e)
        {
            // A committed message is queued, even when the immediate wakeup fails.
            SPDLOG_WARN("[MSG-OUT] Message remains in durable outbox: {}", e.what());
        }
    }
    if (!result) SPDLOG_WARN("[MSG-OUT] lobby={} reason_code={} | Send rejected", LogEvents::lobbyId(getLobbyName()), static_cast<int>(result.error()));
    return result;
}

void DhtChat::markRead(std::size_t count)
{
    m_store->execute(getLobbyName(), lobby::MarkRead{count});
}
void DhtChat::markResynced(std::vector<std::int64_t> sequences)
{
    m_store->execute(getLobbyName(), lobby::MarkResynced{std::move(sequences)});
}
std::string DhtChat::getLobbyName() const
{
    return m_lobby->getLobbyName();
}
