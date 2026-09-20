#pragma once

#include "crypto/HandshakeKeyService.h"
#include "lobby/LobbyObserver.h"
#include "lobby/LobbyStore.h"
#include "network/DhtManager.h"
#include "utils/WorkerThread.h"
#include <memory>
#include <string>

class DhtPublisher
{
  public:
    DhtPublisher() = default;
    ~DhtPublisher() = default;

    void setDhtManager(std::unique_ptr<DhtManager> dhtManager);

    HandshakeKeyService::ChannelKeys deriveChannelKeys(const std::string &passphrase);

    void dispatchHandshakeMessage(const std::string &lobbyName, const std::string &pseudo);
    void dispatchAnomalyMessage(const std::string &lobbyName, const std::string &message);
    void checkLobbyAnomaly(const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore);
    void publishChatMessage(const struct ChatPublishParams &params);
    void enqueueChatMessage(const struct ChatPublishParams &params);
    void waitForPutConfirmed(const std::string &context = "");
    void fetchDhtItem(const std::string &rawKeyString, std::shared_ptr<LobbyObserver> lobbyObs, bool isSeqNum, std::int64_t seqNumTarget = -1);

    lt::session *getSession()
    {
        return m_dhtManager->getSession();
    }

    DhtManager *getDhtManager() const
    {
        return m_dhtManager.get();
    }

    void setMaxConcurrentFetches(int maxFetches)
    {
        m_maxConcurrentFetches = maxFetches;
    }
    
    int getMaxConcurrentFetches() const
    {
        return m_maxConcurrentFetches;
    }

  private:
    std::unique_ptr<DhtManager> m_dhtManager;
    WorkerThread m_networkWorker;
    int m_maxConcurrentFetches = 5;

    void handleDhtItemCallback(const lt::dht_mutable_item_alert* get_mut, std::shared_ptr<LobbyObserver> lobbyObs, std::shared_ptr<LobbyStore> lobbyStore, bool isSeqNum, std::int64_t seqNumTarget);
};
