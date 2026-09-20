#pragma once

#include "LobbyData.h"
#include "LobbyObserver.h"

#include <atomic>
#include <mutex>

class Lobby : public LobbyObserver
{
  public:
    Lobby(const LobbyData &data) : m_lobbyData(data)
    {
    }
    ~Lobby();

  private:
    LobbyData m_lobbyData;
    mutable std::mutex m_mutex;
    std::atomic<bool> m_hasUpdate{false};
    std::atomic<bool> m_isSeqFetching{false};
    std::map<std::int64_t, std::int64_t> m_pendingResyncs;

  public:
    void onUpdate(const nlohmann::json &jsonPatch) override;
    std::string getLobbyName() const override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lobbyData.lobby_name;
    }
    LobbyData getLobbyData() const override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lobbyData;
    }
    void setFetchingSeq() override
    {
        m_isSeqFetching.store(true, std::memory_order_relaxed);
    }
    void clearFetchingSeq() override
    {
        m_isSeqFetching.store(false, std::memory_order_relaxed);
    }
    bool consumeUpdateFlag() override
    {
        return m_hasUpdate.exchange(false);
    }
    bool isFetchingSeq() const override
    {
        return m_isSeqFetching.load(std::memory_order_relaxed);
    }
};