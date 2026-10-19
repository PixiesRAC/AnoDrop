#pragma once
#include <functional>

#include "DhtChatUtils.h"
#include "DhtTypes.h"

// Asynchronous boundary. shutdown must finish/cancel callbacks before its clients die.
class DhtTransport
{
  public:
    using DhtItemCallback = std::function<void(DhtFetchResult)>;
    using PutCompletion = std::function<void(bool)>;
    virtual ~DhtTransport() = default;
    virtual bool fetchDhtItem(const std::string &key, const std::string &lobby,
                              DhtItemCallback callback, DhtRequest request = {}) = 0;
    virtual bool publishHandshake(const std::string &lobby, const std::string &payload,
                                  std::int64_t sequence, PutCompletion completion) = 0;
    virtual bool publishChatMessage(const ChatPublishParams &params,
                                    PutCompletion completion = {}) = 0;
    virtual bool publishSequenceAnnouncement(const ChatPublishParams &params) = 0;
    virtual bool publishAnomaly(const std::string &lobby, const std::string &reason) = 0;
    virtual bool publishHeartbeat(int offsetSeconds, const std::string &lobby, const std::string &key) = 0;
    virtual bool publishAck(const std::string &message, const std::string &lobby, const std::string &key, int offsetSeconds) = 0;
};
