#pragma once
#include "DhtChatUtils.h"

class OutgoingMessageQueue
{
  public:
    virtual ~OutgoingMessageQueue() = default;
    virtual void enqueueChatMessage(const ChatPublishParams &params) = 0;
};
