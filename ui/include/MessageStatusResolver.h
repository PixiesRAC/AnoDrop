#pragma once

#include "LobbyData.h"
#include <map>
#include <span>
#include <string>
#include <vector>

namespace ui
{

class MessageStatusResolver
{
  public:
    static constexpr const char* STATUS_ACK_VIA_RESYNC = " [Ack (via resync)]";
    static constexpr const char* STATUS_ACK = " [Ack]";
    static constexpr const char* STATUS_SENT_VIA_RESYNC = " [Sent (via resync)]";
    static constexpr const char* STATUS_SENDING_RESYNC = " [Sending resync...]";
    static constexpr const char* STATUS_SENT = " [Sent]";
    static constexpr const char* STATUS_SENDING = " [Sending...]";

    static void buildResyncMaps(std::span<const ChatMessage> messages,
                                std::map<std::int64_t, std::vector<std::int64_t>> &origToResyncs,
                                std::map<std::int64_t, std::int64_t> &resyncToOrig);

    [[nodiscard]] static std::pair<std::string, int> resolveStatus(const ChatMessage &msg,
                                                     const LobbyViewData &lobbyData,
                                                     const std::map<std::int64_t, std::vector<std::int64_t>> &origToResyncs,
                                                     const std::map<std::int64_t, std::int64_t> &resyncToOrig);
};

} // namespace ui
