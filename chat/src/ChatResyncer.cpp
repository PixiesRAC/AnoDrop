#include "ChatResyncer.h"
#include "LogEvents.h"
#include <spdlog/spdlog.h>

#include <ctime>
#include <format>

#include "ChatConstants.h"

namespace chat
{

int ChatResyncer::resyncPendingMessages(LobbyObserver &lobbyObserver, DhtChat &dhtChat)
{
    // Explicit resync keeps its existing wire format; only selection is shared with the outbox.
    SPDLOG_INFO("[USER] lobby={} | Manual resync requested", LogEvents::lobbyId(lobbyObserver.getLobbyName()));
    auto toResend = lobbyObserver.pendingOutgoing(0, DhtChat::RATE_LIMIT_MAX_MESSAGES);

    std::vector<std::int64_t> resyncedOriginals;

    int skippedCount = 0;
    for (const auto &msg : toResend)
    {
        std::time_t t = msg.fake_timestamp;
        std::tm tm_info{};
        gmtime_r(&t, &tm_info);

        char timeBuf[16]{};
        std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm_info);

        // TODO (Protocol Debt): Prefixing resync metadata directly into plaintext content alters user payload
        // and could collide with genuine user messages.
        // Target architecture: Move retransmission metadata to an explicit wire protocol envelope / header field
        // (e.g., `retransmit_seq_num` in the DHT payload) rather than mutating the user message text.
        std::string newContent = std::format("{}{} {}] {}", chat::RESYNC_PREFIX, msg.seq_num, timeBuf, msg.content);
        auto newSeq = dhtChat.sendMessage(newContent);
        if (newSeq)
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
        dhtChat.markResynced(std::move(resyncedOriginals));
    }
    
    SPDLOG_INFO("[RESYNC] lobby={} resent={} skipped={} | Manual resync completed", LogEvents::lobbyId(lobbyObserver.getLobbyName()), toResend.size() - skippedCount, skippedCount);
    return skippedCount;
}

} // namespace chat
