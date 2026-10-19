#pragma once
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "DhtTypes.h"
#include "LobbyObserver.h"
#include "ReceivePlanner.h"
class DhtTransport;
class LobbyStore;
class PartitionedThreadPool;

class MessageReceiver
{
  public:
    MessageReceiver(DhtTransport &manager, LobbyStore &store, PartitionedThreadPool &executor,
                    std::size_t fetchBudget)
        : m_manager(manager), m_store(store), m_executor(executor), m_fetchBudget(fetchBudget)
    {
        if (fetchBudget == 0 || fetchBudget > 64)
            throw std::invalid_argument("Invalid receive budget");
    }
    bool fetchLatestSequence(std::shared_ptr<LobbyObserver> lobby);

  private:
    bool fetchMessage(const std::string &key, std::shared_ptr<LobbyObserver> lobby, std::int64_t sequence);
    void receiveSequence(std::shared_ptr<LobbyObserver> lobby, const DhtFetchResult &result);
    void receiveMessage(std::shared_ptr<LobbyObserver> lobby, std::int64_t sequence, const DhtFetchResult &result);
    void fetchMissingMessages(std::int64_t announced, const LobbyData &data, std::shared_ptr<LobbyObserver> lobby);
    DhtTransport &m_manager;
    LobbyStore &m_store;
    PartitionedThreadPool &m_executor;
    const std::size_t m_fetchBudget;
    std::mutex m_mutex;
    std::unordered_set<std::string> m_pendingSequences;
    std::unordered_map<std::string, ReceivePlanner> m_receivePlanners;
};
