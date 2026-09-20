#include "DhtChat.h"

#include "chat/DhtChatUtils.h"
#include "core/DhtPublisher.h"
#include "lobby/LobbyStore.h"

#include <chrono>
#include <spdlog/spdlog.h>

DhtChat::DhtChat(Lobby &lobby, std::shared_ptr<DhtPublisher> pub) : m_lobby(lobby), m_publisher(pub)
{
    SPDLOG_INFO("DhtChat: Creating chat for lobby {}", m_lobby.getLobbyData().lobby_name);
}

DhtChat::~DhtChat()
{
}

namespace
{
int64_t computeFakeTimestamp(int offset_seconds)
{
    auto now = std::chrono::system_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return epoch + offset_seconds;
}
} // namespace

std::int64_t DhtChat::sendMessage(const std::string &message)
{
    LobbyData data = m_lobby.getLobbyData();
    SPDLOG_INFO("DhtChat: Sending message to lobby {}: {}", data.lobby_name, message);

    std::int64_t newSeqNum = data.my_seq_num + 1;
    ChatMessage chatMsg;
    chatMsg.direction = MessageDirection::OUT;
    chatMsg.content = message;
    chatMsg.fake_timestamp = computeFakeTimestamp(data.fake_timezone.offset_seconds);
    chatMsg.seq_num = newSeqNum;

    nlohmann::json msgJson = chatMsg;
    std::string payloadStr = msgJson.dump();

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
    if (m_publisher && m_publisher->getDhtManager() && m_publisher->getDhtManager()->getLobbyStore())
    {
        m_publisher->getDhtManager()->getLobbyStore()->asyncUpdateLobbyData(m_lobby.getLobbyData().lobby_name, patch);
    }
    else
    {
        m_lobby.onUpdate(patch);
    }
}