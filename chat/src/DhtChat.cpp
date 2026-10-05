#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "DhtChat.h"

#include <spdlog/spdlog.h>


#include "DhtChatUtils.h"
#include "DhtPublisher.h"

DhtChat::DhtChat(Lobby &lobby, std::shared_ptr<DhtPublisher> pub) : m_lobby(lobby), m_publisher(pub)
{
    SPDLOG_INFO("DhtChat: Creating chat for lobby {}", m_lobby.getLobbyData().lobby_name);
    republishUnackedMessages();
}

DhtChat::~DhtChat()
{
}

void DhtChat::republishUnackedMessages()
{
    LobbyData data = m_lobby.getLobbyData();
    for (auto it = data.history.messages.rbegin(); it != data.history.messages.rend(); ++it)
    {
        const auto& msg = *it;
        if (msg.direction == MessageDirection::OUT)
        {
            if (msg.seq_num > 0 && static_cast<std::uint64_t>(msg.seq_num) <= data.peer_acked_seq_nums.through)
            {
                break; // Everything older is mathematically guaranteed to be acknowledged!
            }

            if (!data.peer_acked_seq_nums.acknowledges(msg.seq_num))
            {
                SPDLOG_INFO("Outbox: Republishing unacknowledged message SeqNum {}", msg.seq_num);
                nlohmann::json msgJson = msg;
                std::string payloadStr = msgJson.dump();

                ChatPublishParams params;
                params.encryptedPayload = payloadStr;
                params.lobbyName = data.lobby_name;
                params.targetSeqNum = msg.seq_num;
                params.sequenceTargetKey = data.private_node_discussion + data.my_x25519_pub;
                params.announcedTargetKey = data.private_node_discussion + data.announced_x25519_pub;
                params.messageTargetKey = data.private_node_discussion + data.my_x25519_pub + std::to_string(msg.seq_num);

                if (m_publisher)
                {
                    m_publisher->enqueueChatMessage(params);
                }
            }
        }
    }
}

std::int64_t DhtChat::sendMessage(const std::string &message)
{
    LobbyData data = m_lobby.getLobbyData();

    std::int64_t currentFakeTime = TimeUtils::computeFakeTimestamp(data.fake_timezone.offset_seconds);
    int recentMessagesCount = 0;

    for (auto it = data.history.messages.rbegin(); it != data.history.messages.rend(); ++it)
    {
        if (it->direction == MessageDirection::OUT)
        {
            if (currentFakeTime - it->fake_timestamp <= RATE_LIMIT_WINDOW_SECONDS)
            {
                recentMessagesCount++;
            }
            else
            {
                break; // Since our OUT messages are chronologically ordered
            }
        }
    }

    if (recentMessagesCount >= RATE_LIMIT_MAX_MESSAGES)
    {
        SPDLOG_WARN("DhtChat: Rate limit exceeded for lobby {}. Max {} messages per {} seconds.", 
                    data.lobby_name, RATE_LIMIT_MAX_MESSAGES, RATE_LIMIT_WINDOW_SECONDS);
        return -1;
    }

    SPDLOG_INFO("DhtChat: Sending message to lobby {}: {}", data.lobby_name, message);

    std::int64_t newSeqNum = data.my_seq_num + 1;
    ChatMessage chatMsg;
    chatMsg.direction = MessageDirection::OUT;
    chatMsg.content = message;
    chatMsg.fake_timestamp = TimeUtils::computeFakeTimestamp(data.fake_timezone.offset_seconds);
    chatMsg.seq_num = newSeqNum;

    nlohmann::json msgJson = chatMsg;
    std::string payloadStr = msgJson.dump();

    size_t expectedBencodeSize = std::to_string(payloadStr.size()).length() + 1 + payloadStr.size();
    if (expectedBencodeSize > DhtManager::MAX_BEP44_PAYLOAD_SIZE)
    {
        SPDLOG_WARN("DhtChat: Message too large for BEP 44 ({} bytes). Rejected.", expectedBencodeSize);
        return -1;
    }

    ChatPublishParams params;
    params.encryptedPayload = payloadStr; // Will be encrypted later
    params.lobbyName = data.lobby_name;
    params.targetSeqNum = newSeqNum;
    params.sequenceTargetKey = data.private_node_discussion + data.my_x25519_pub;
    params.announcedTargetKey = data.private_node_discussion + data.announced_x25519_pub;
    params.messageTargetKey = data.private_node_discussion + data.my_x25519_pub + std::to_string(newSeqNum);

    if (m_publisher)
    {
        nlohmann::json patch;
        patch["new_message"] = chatMsg;
        patch["my_seq_num"] = newSeqNum;

        dispatchPatch(patch);

        SPDLOG_INFO("DhtChat: Message persisted to history for lobby {}", data.lobby_name);

        SPDLOG_DEBUG("🚀 [NETWORK SEND] Network Worker: Enqueueing DHT publish task for message '{}' (SeqNum: {})", chatMsg.content, newSeqNum);
        m_publisher->enqueueChatMessage(params);
    }
    else
    {
        SPDLOG_ERROR("DhtChat::sendMessage failed: Publisher is null");
    }

    return newSeqNum;
}

void DhtChat::dispatchPatch(const nlohmann::json &patch)
{
    nlohmann::json mutablePatch = patch;
    if (m_publisher && m_publisher->getDhtManager() && m_publisher->getDhtManager()->getLobbyStore())
    {
        m_publisher->getDhtManager()->getLobbyStore()->asyncUpdateLobbyData(m_lobby.getLobbyData().lobby_name, mutablePatch);
    }
    else
    {
        m_lobby.onUpdate(mutablePatch);
    }
}