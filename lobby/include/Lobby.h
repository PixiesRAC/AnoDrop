#pragma once

#include <nlohmann/json.hpp>
#include <atomic>
#include <shared_mutex>

#include "LobbyObserver.h"

class Lobby : public LobbyObserver
{
  public:
    Lobby(const LobbyData &data) : m_lobbyData(data)
    {
    }
    ~Lobby();

  private:
    LobbyData m_lobbyData;
    mutable std::shared_mutex m_mutex;
    std::atomic<bool> m_hasUpdate{false};
    std::atomic<bool> m_isSeqFetching{false};
    std::map<std::int64_t, std::int64_t> m_pendingResyncs;

  public:
    void onUpdate(nlohmann::json &jsonPatch) override;
    [[nodiscard]] std::string getLobbyName() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.lobby_name;
    }
    [[nodiscard]] LobbyData getLobbyData() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData;
    }
    [[nodiscard]] LobbyData getLobbyDataWithoutHistory() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.cloneWithoutHistory();
    }
    [[nodiscard]] LobbyMetadata getMetadata() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.getMetadata();
    }
    [[nodiscard]] LobbyStatus getStatus() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.status;
    }
    [[nodiscard]] bool isPeerConnected() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.isPeerConnected();
    }
    [[nodiscard]] LobbyNetworkState getNetworkState() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.getNetworkState();
    }
    [[nodiscard]] LobbyCryptoVault getCryptoVault() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.getCryptoVault();
    }
    void withHistory(HistoryReader reader) const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        reader(std::span<const ChatMessage>(m_lobbyData.history.messages));
    }
    void withDataView(DataViewReader reader) const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        reader(m_lobbyData.getMetadata(), m_lobbyData.getSyncState(), m_lobbyData.getCryptoVault());
    }
    void setFetchingSeq() override
    {
        m_isSeqFetching.store(true, std::memory_order_relaxed);
    }
    void clearFetchingSeq() override
    {
        m_isSeqFetching.store(false, std::memory_order_relaxed);
    }
    [[nodiscard]] bool consumeUpdateFlag() override
    {
        return m_hasUpdate.exchange(false);
    }
    [[nodiscard]] bool isFetchingSeq() const override
    {
        return m_isSeqFetching.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool hasUnreadMessages() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.history.messages.size() > m_lobbyData.read_messages_count;
    }
};