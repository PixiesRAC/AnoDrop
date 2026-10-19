#include "MessageStatusResolver.h"
#include "ChatConstants.h"
#include <algorithm>
#include <charconv>
#include <string_view>

namespace ui
{

void MessageStatusResolver::buildResyncMaps(std::span<const ChatMessage> messages,
                                            std::map<std::int64_t, std::vector<std::int64_t>> &origToResyncs,
                                            std::map<std::int64_t, std::int64_t> &resyncToOrig)
{
    for (const auto &msg : messages)
    {
        if (msg.direction == MessageDirection::OUT && msg.content.starts_with(chat::RESYNC_PREFIX))
        {
            const std::string_view contentView(msg.content);
            const size_t start = chat::RESYNC_PREFIX.length();
            const size_t end = contentView.find(' ', start);
            if (end != std::string_view::npos)
            {
                const std::string_view seqToken = contentView.substr(start, end - start);
                std::int64_t origSeq = 0;
                const auto [ptr, ec] = std::from_chars(seqToken.data(), seqToken.data() + seqToken.size(), origSeq);
                if (ec == std::errc{} && ptr == seqToken.data() + seqToken.size() && origSeq > 0)
                {
                    origToResyncs[origSeq].push_back(msg.seq_num);
                    resyncToOrig[msg.seq_num] = origSeq;
                }
            }
        }
    }
}

std::pair<std::string, int> MessageStatusResolver::resolveStatus(const ChatMessage &msg,
                                                                 const LobbyViewData &lobbyData,
                                                                 const std::map<std::int64_t, std::vector<std::int64_t>> &origToResyncs,
                                                                 const std::map<std::int64_t, std::int64_t> &resyncToOrig)
{
    if (msg.direction != MessageDirection::OUT)
        return {"", 0};

    auto isSeqAcked = [&](std::int64_t s) {
        return lobbyData.sync.peer_acked_seq_nums.acknowledges(s);
    };

    bool isPeerAcked = isSeqAcked(msg.seq_num);

    const auto origIt = origToResyncs.find(msg.seq_num);
    const bool hasResyncChildren = (origIt != origToResyncs.end());

    if (!isPeerAcked && hasResyncChildren)
    {
        for (auto rSeq : origIt->second)
        {
            if (isSeqAcked(rSeq))
            {
                isPeerAcked = true;
                break;
            }
        }
    }

    if (!isPeerAcked)
    {
        if (const auto resyncIt = resyncToOrig.find(msg.seq_num); resyncIt != resyncToOrig.end())
        {
            if (isSeqAcked(resyncIt->second))
            {
                isPeerAcked = true;
            }
        }
    }

    bool isAcked = lobbyData.sync.dht_acked_seq_nums.acknowledges(msg.seq_num);
    bool isResynced = std::binary_search(
        lobbyData.sync.resynced_seq_nums.begin(), lobbyData.sync.resynced_seq_nums.end(), msg.seq_num);

    bool anyResyncAcked = false;
    if (hasResyncChildren)
    {
        for (auto rSeq : origIt->second)
        {
            if (lobbyData.sync.dht_acked_seq_nums.acknowledges(rSeq))
            {
                anyResyncAcked = true;
                break;
            }
        }
    }

    if (isPeerAcked)
    {
        if (isResynced || hasResyncChildren)
            return {STATUS_ACK_VIA_RESYNC, 3};
        else
            return {STATUS_ACK, 3};
    }
    else if (isResynced || hasResyncChildren)
    {
        if (anyResyncAcked)
            return {STATUS_SENT_VIA_RESYNC, 5};
        else
            return {STATUS_SENDING_RESYNC, 4};
    }
    else if (isAcked)
    {
        return {STATUS_SENT, 2};
    }
    else
    {
        return {STATUS_SENDING, 4};
    }
}

} // namespace ui
