#include "MessageStatusResolver.h"
#include "ChatConstants.h"
#include <algorithm>

namespace ui
{

void MessageStatusResolver::buildResyncMaps(const std::vector<ChatMessage> &messages,
                                            std::map<std::int64_t, std::vector<std::int64_t>> &origToResyncs,
                                            std::map<std::int64_t, std::int64_t> &resyncToOrig)
{
    for (const auto &msg : messages)
    {
        if (msg.direction == MessageDirection::OUT && msg.content.find(chat::RESYNC_PREFIX) == 0)
        {
            size_t start = chat::RESYNC_PREFIX.length();
            size_t end = msg.content.find(" ", start);
            if (end != std::string::npos)
            {
                try
                {
                    std::int64_t origSeq = std::stoll(msg.content.substr(start, end - start));
                    origToResyncs[origSeq].push_back(msg.seq_num);
                    resyncToOrig[msg.seq_num] = origSeq;
                }
                catch (...)
                {
                }
            }
        }
    }
}

std::pair<std::string, int> MessageStatusResolver::resolveStatus(const ChatMessage &msg,
                                                                 const LobbyData &lobbyData,
                                                                 const std::map<std::int64_t, std::vector<std::int64_t>> &origToResyncs,
                                                                 const std::map<std::int64_t, std::int64_t> &resyncToOrig)
{
    if (msg.direction != MessageDirection::OUT)
        return {"", 0};

    auto isSeqAcked = [&](std::int64_t s) {
        return lobbyData.peer_acked_seq_nums.acknowledges(s);
    };

    bool isPeerAcked = isSeqAcked(msg.seq_num);

    if (!isPeerAcked && origToResyncs.count(msg.seq_num))
    {
        for (auto rSeq : origToResyncs.at(msg.seq_num))
        {
            if (isSeqAcked(rSeq))
            {
                isPeerAcked = true;
                break;
            }
        }
    }

    if (!isPeerAcked && resyncToOrig.count(msg.seq_num))
    {
        if (isSeqAcked(resyncToOrig.at(msg.seq_num)))
        {
            isPeerAcked = true;
        }
    }

    bool isAcked = lobbyData.dht_acked_seq_nums.acknowledges(msg.seq_num);
    bool isResynced = std::binary_search(
        lobbyData.resynced_seq_nums.begin(), lobbyData.resynced_seq_nums.end(), msg.seq_num);

    bool anyResyncAcked = false;
    if (origToResyncs.count(msg.seq_num))
    {
        for (auto rSeq : origToResyncs.at(msg.seq_num))
        {
            if (lobbyData.dht_acked_seq_nums.acknowledges(rSeq))
            {
                anyResyncAcked = true;
                break;
            }
        }
    }

    if (isPeerAcked)
    {
        if (isResynced || origToResyncs.count(msg.seq_num))
            return {STATUS_ACK_VIA_RESYNC, 3};
        else
            return {STATUS_ACK, 3};
    }
    else if (isResynced || origToResyncs.count(msg.seq_num))
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
