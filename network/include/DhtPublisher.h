#pragma once

#include "DhtManager.h"
#include "HandshakeKeyService.h"
#include "PartitionedThreadPool.h"

class DhtPublisher
{
  public:
    DhtPublisher() = default;
    ~DhtPublisher();
    
    void shutdown();

    void setDhtManager(std::unique_ptr<DhtManager> dhtManager);

    [[nodiscard]] HandshakeKeyService::ChannelKeys deriveChannelKeys(const std::string &passphrase);

    void dispatchHandshakeMessage(const std::string &lobbyName, const std::string &pseudo);
    void publishChatMessage(const struct ChatPublishParams &params);
    void enqueueChatMessage(const struct ChatPublishParams &params);
    void fetchDhtItem(const std::string &rawKeyString, std::shared_ptr<LobbyObserver> lobbyObs, bool isSeqNum, std::int64_t seqNumTarget = -1);

    [[nodiscard]] DhtManager *getDhtManager() const
    {
        return m_dhtManager.get();
    }

    void enqueueNetworkTask(const std::string& lobbyName, std::function<void()> task)
    {
        (void)m_networkWorker.enqueue(lobbyName, std::move(task));
    }

    void setMaxConcurrentFetches(int maxFetches)
    {
        m_maxConcurrentFetches = maxFetches;
    }

    [[nodiscard]] int getMaxConcurrentFetches() const
    {
        return m_maxConcurrentFetches;
    }

  private:
    std::mutex m_fetchAttemptsMutex;
    struct FetchAttempt {
        std::chrono::steady_clock::time_point lastAttempt;
        int attemptCount;
    };
    std::unordered_map<std::string, std::unordered_map<std::int64_t, FetchAttempt>> m_missingFetchAttempts;

    std::unique_ptr<DhtManager> m_dhtManager;
    PartitionedThreadPool m_networkWorker{4};
    int m_maxConcurrentFetches = 5;

    void handleDhtItemCallback(std::shared_ptr<LobbyObserver> lobbyObs, std::shared_ptr<LobbyStore> lobbyStore, bool isSeqNum, std::int64_t seqNumTarget, bool isUndefined, const std::string& valueStr, std::int64_t dhtSeq);
    void fetchMissingMessages(std::int64_t maxSeq, const LobbyData& lobbyData, std::shared_ptr<LobbyObserver> lobbyObs);
    void processSeqNumCallback(std::int64_t dhtSeq, const LobbyData& lobbyData, std::shared_ptr<LobbyObserver> lobbyObs, std::shared_ptr<LobbyStore> lobbyStore, bool isUndefined);
    void processMessagePayloadCallback(const LobbyData& lobbyData, std::shared_ptr<LobbyObserver> lobbyObs, std::shared_ptr<LobbyStore> lobbyStore, bool isUndefined, const std::string& valueStr, std::int64_t seqNumTarget);
};
