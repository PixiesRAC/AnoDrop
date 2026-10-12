#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "ChatResyncer.h"
#include "ChatConstants.h"

namespace chat
{

int ChatResyncer::resyncPendingMessages(LobbyObserver &lobbyObserver, DhtChat &dhtChat)
{
    LobbyData data = lobbyObserver.getLobbyDataWithoutHistory();

    std::vector<ChatMessage> toResend;

    lobbyObserver.withHistory([&](std::span<const ChatMessage> messages) {
        for (const auto &msg : messages)
        {
            if (msg.direction == MessageDirection::OUT)
            {
                bool isAcked = data.peer_acked_seq_nums.acknowledges(msg.seq_num);

                bool isResynced = std::binary_search(
                    data.resynced_seq_nums.begin(),
                    data.resynced_seq_nums.end(),
                    msg.seq_num);

                if (!isAcked && !isResynced)
                {
                    toResend.push_back(msg);
                }
            }
        }
    });

    std::vector<std::int64_t> resyncedOriginals;

    int skippedCount = 0;
    for (const auto &msg : toResend)
    {
        std::time_t t = msg.fake_timestamp;
        std::tm tm_info{};
        localtime_r(&t, &tm_info);

        std::ostringstream timeStream;
        timeStream << std::put_time(&tm_info, "%H:%M:%S");

        // TODO (Protocol Debt): Prefixing resync metadata directly into plaintext content alters user payload
        // and could collide with genuine user messages.
        // Target architecture: Move retransmission metadata to an explicit wire protocol envelope / header field
        // (e.g., `retransmit_seq_num` in the DHT payload) rather than mutating the user message text.
        std::string newContent = std::string(chat::RESYNC_PREFIX) + std::to_string(msg.seq_num) + " " + timeStream.str() + "] " + msg.content;
        std::int64_t newSeq = dhtChat.sendMessage(newContent);
        if (newSeq != -1)
        {
            resyncedOriginals.push_back(msg.seq_num);
        }
        else
        {
            skippedCount++;
        }
    }

    if (!resyncedOriginals.empty())
    {
        nlohmann::json patch;
        patch["message_resynced"] = resyncedOriginals;
        dhtChat.dispatchPatch(patch);
    }
    
    return skippedCount;
}

} // namespace chat
