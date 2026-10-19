#pragma once
#include <string>
#include "DhtChatUtils.h"
class DhtTransport;
class LobbyStore;
class PartitionedThreadPool;

class OutboxPublisher {
public:
    OutboxPublisher(DhtTransport& manager, LobbyStore& store, PartitionedThreadPool& executor)
        : m_manager(manager), m_store(store), m_executor(executor) {}
    void publishChatMessage(const ChatPublishParams& params);
private:
    DhtTransport& m_manager;
    LobbyStore& m_store;
    PartitionedThreadPool& m_executor;
};
