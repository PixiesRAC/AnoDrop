#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>

#include "LobbyObserver.h"
#include "LobbyUpdate.h"

class Lobby : public LobbyObserver
{
  public:
    explicit Lobby(const LobbyData &data) : m_lobbyName(data.metadata.lobby_name), m_lobbyData(data)
    {
        for (std::size_t i = 0; i < data.history.records.size(); ++i)
        {
            const auto &message = data.history.records[i].metadata;
            m_messageIds.emplace(message.direction, message.seq_num);
            if (message.direction == MessageDirection::OUT)
                m_outgoingIndex.emplace(message.seq_num, i);
        }
    }
    ~Lobby();

  private:
    LobbyUpdate normalizeUpdateUnlocked(LobbyUpdate update) const;
    const std::string m_lobbyName;
    LobbyData m_lobbyData;
    std::set<std::pair<MessageDirection, std::int64_t>> m_messageIds;
    std::map<std::int64_t, std::size_t> m_outgoingIndex;
    mutable std::shared_mutex m_mutex;
    std::atomic<bool> m_hasUpdate{false};

    [[nodiscard]] LobbyUpdate normalizeUpdate(LobbyUpdate update) const override
    {
        std::shared_lock lock(m_mutex);
        return normalizeUpdateUnlocked(std::move(update));
    }
    void onUpdate(LobbyUpdate &update, MessageLoader loader = {}) override;
    void resetHistoryRecords(std::vector<HistoryRecord> records) override
    {
        // Build everything before publishing. Allocation failure leaves the
        // current history and both indexes untouched.
        decltype(m_messageIds) messageIds;
        decltype(m_outgoingIndex) outgoingIndex;
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto& meta = records[i].metadata;
            if (meta.seq_num <= 0 ||
                (meta.direction != MessageDirection::IN && meta.direction != MessageDirection::OUT) ||
                !messageIds.emplace(meta.direction, meta.seq_num).second)
                throw std::invalid_argument("Invalid replacement history");
            if (meta.direction == MessageDirection::OUT) outgoingIndex.emplace(meta.seq_num, i);
        }
        std::unique_lock lock(m_mutex);
        m_lobbyData.history.records.swap(records);
        m_messageIds.swap(messageIds);
        m_outgoingIndex.swap(outgoingIndex);
    }

  public:
    [[nodiscard]] std::string getLobbyName() const override
    {
        return m_lobbyName;
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
        return m_lobbyData.metadata;
    }
    [[nodiscard]] LobbyStatus getStatus() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.metadata.status;
    }
    [[nodiscard]] bool isPeerConnected() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.isPeerConnected();
    }
    [[nodiscard]] LobbySyncState getNetworkState() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.sync;
    }
    [[nodiscard]] LobbyViewData getViewData() const override
    {
        std::shared_lock lock(m_mutex);
        return {m_lobbyData.metadata, m_lobbyData.sync};
    }
    [[nodiscard]] std::size_t historySize() const override
    {
        std::shared_lock lock(m_mutex);
        return m_lobbyData.history.records.size();
    }
    [[nodiscard]] std::vector<ChatMessage> historyPage(std::size_t offset, std::size_t count) const override
    {
        std::vector<HistoryRecord> records;
        {
            std::shared_lock lock(m_mutex);
            const auto &history = m_lobbyData.history.records;
            offset = std::min(offset, history.size());
            count = std::min(count, history.size() - offset);
            records.assign(history.begin() + static_cast<std::ptrdiff_t>(offset), history.begin() + static_cast<std::ptrdiff_t>(offset + count));
        }
        std::vector<ChatMessage> messages;
        messages.reserve(records.size());
        for (const auto &record : records)
            messages.push_back(record.read());
        return messages;
    }
    [[nodiscard]] std::vector<ChatMessage> pendingOutgoing(std::int64_t afterSequence, std::size_t count) const override
    {
        std::vector<HistoryRecord> records;
        {
            std::shared_lock lock(m_mutex);
            const auto &state = m_lobbyData.sync;
            for (auto it = m_outgoingIndex.upper_bound(std::max(afterSequence, static_cast<std::int64_t>(state.peer_acked_seq_nums.through))); it != m_outgoingIndex.end() && records.size() < count; ++it)
            {
                if (state.peer_acked_seq_nums.acknowledges(static_cast<std::uint64_t>(it->first)) ||
                    std::binary_search(state.resynced_seq_nums.begin(), state.resynced_seq_nums.end(), it->first))
                    continue;
                records.push_back(m_lobbyData.history.records[it->second]);
            }
        }
        std::vector<ChatMessage> messages;
        for (const auto &record : records)
            messages.push_back(record.read());
        return messages;
    }
    [[nodiscard]] std::vector<std::int64_t> pendingOutgoingSequences(std::int64_t afterSequence, std::size_t count) const override
    {
        std::vector<std::int64_t> sequences;
        std::shared_lock lock(m_mutex);
        const auto &state = m_lobbyData.sync;
        for (auto it = m_outgoingIndex.upper_bound(std::max(afterSequence, static_cast<std::int64_t>(state.peer_acked_seq_nums.through)));
             it != m_outgoingIndex.end() && sequences.size() < count; ++it)
        {
            if (state.peer_acked_seq_nums.acknowledges(static_cast<std::uint64_t>(it->first)) ||
                std::binary_search(state.resynced_seq_nums.begin(), state.resynced_seq_nums.end(), it->first))
                continue;
            sequences.push_back(it->first);
        }
        return sequences;
    }
    [[nodiscard]] std::optional<ChatMessage> getOutgoingMessage(std::int64_t sequence) const override
    {
        std::optional<HistoryRecord> record;
        {
            std::shared_lock lock(m_mutex);
            auto it = m_outgoingIndex.find(sequence);
            if (it != m_outgoingIndex.end() && it->second < m_lobbyData.history.records.size())
                record = m_lobbyData.history.records[it->second];
        }
        if (record)
            return record->read();
        return std::nullopt;
    }
    void withDataView(DataViewReader reader) const override
    {
        const auto snapshot = getLobbyDataWithoutHistory();
        static const LobbyCryptoVault emptyVault{};
        reader(snapshot.metadata, snapshot.sync, snapshot.crypto ? *snapshot.crypto : emptyVault);
    }
    [[nodiscard]] bool consumeUpdateFlag() override
    {
        return m_hasUpdate.exchange(false);
    }
    [[nodiscard]] bool hasUnreadMessages() const override
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_lobbyData.history.records.size() > m_lobbyData.metadata.read_messages_count;
    }
};