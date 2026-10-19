#pragma once
#include <string>
class DhtTransport;
class LobbyStore;
class PartitionedThreadPool;

class HandshakeCoordinator {
public:
    HandshakeCoordinator(DhtTransport& manager, LobbyStore& store, PartitionedThreadPool& executor)
        : m_manager(manager), m_store(store), m_executor(executor) {}
    void dispatchHandshake(const std::string& lobbyName, const std::string& pseudo);
private:
    DhtTransport& m_manager;
    LobbyStore& m_store;
    PartitionedThreadPool& m_executor;
};
