#pragma once
#include "ThreadAffinity.h"

#include "DhtManager.h"
#include "LobbyStore.h"
#include "HandshakeKeyService.h"
#include "PartitionedThreadPool.h"
#include "OutgoingMessageQueue.h"
#include <unordered_set>

class MessageReceiver;
class HandshakeCoordinator;
class OutboxPublisher;

class DhtPublisher : public OutgoingMessageQueue
{
  public:
    DhtPublisher(std::shared_ptr<LobbyStore> store, std::unique_ptr<DhtManager> manager, int fetchBudget);
    ~DhtPublisher() override;
    
    void shutdown();

    [[nodiscard]] HandshakeKeyService::ChannelKeys deriveChannelKeys(const std::string &passphrase, const std::string &lobby = "");

    void dispatchHandshakeMessage(const std::string &lobbyName, const std::string &pseudo);
    void enqueueChatMessage(const ChatPublishParams &params) override;
    bool fetchLatestSequence(std::shared_ptr<LobbyObserver> lobby);

    [[nodiscard]] DhtManager *getDhtManager() const
    {
        return m_dhtManager.get();
    }

    void enqueueNetworkTask(const std::string& lobbyName, std::function<void()> task);
    bool enqueuePollingTask(const std::string& lobby, const std::string& purpose, std::function<void()> task);

  private:
    ThreadAffinity m_ownerThread;
    std::mutex m_pollMutex;
    std::unordered_set<std::string> m_pendingPolls;

    std::shared_ptr<LobbyStore> m_lobbyStore;
    std::unique_ptr<DhtManager> m_dhtManager;
    PartitionedThreadPool m_networkWorker{4};
    std::unique_ptr<HandshakeCoordinator> m_handshakeCoordinator;
    std::unique_ptr<OutboxPublisher> m_outboxPublisher;

    std::unique_ptr<MessageReceiver> m_messageReceiver;
};
