#pragma once
#include "LobbyUpdate.h"

#include <functional>
#include <string>

#include "LobbyData.h"

class LobbyObserver
{
  public:
    LobbyObserver() = default;
    virtual ~LobbyObserver() = default;

    [[nodiscard]] virtual std::string getLobbyName() const = 0;
    [[nodiscard]] virtual LobbyData getLobbyData() const = 0;
    [[nodiscard]] virtual LobbyData getLobbyDataWithoutHistory() const = 0;
    [[nodiscard]] virtual LobbyMetadata getMetadata() const = 0;
    [[nodiscard]] virtual LobbyStatus getStatus() const = 0;
    [[nodiscard]] virtual bool isPeerConnected() const = 0;
    [[nodiscard]] virtual LobbySyncState getNetworkState() const = 0;

    [[nodiscard]] virtual LobbyViewData getViewData() const = 0;

    [[nodiscard]] virtual std::size_t historySize() const = 0;
    [[nodiscard]] virtual std::vector<ChatMessage> historyPage(std::size_t offset, std::size_t count) const = 0;
    [[nodiscard]] virtual std::vector<ChatMessage> pendingOutgoing(std::int64_t afterSequence, std::size_t count) const = 0;
    [[nodiscard]] virtual std::vector<std::int64_t> pendingOutgoingSequences(std::int64_t afterSequence, std::size_t count) const = 0;
    [[nodiscard]] virtual std::optional<ChatMessage> getOutgoingMessage(std::int64_t sequence) const = 0;


    using DataViewReader = std::function<void(const LobbyMetadata &, const LobbySyncState &, const LobbyCryptoVault &)>;
    virtual void withDataView(DataViewReader reader) const = 0;

    [[nodiscard]] virtual bool consumeUpdateFlag() = 0;
    [[nodiscard]] virtual bool hasUnreadMessages() const = 0;
private:
    // Only the repository and dedicated test attorney may mutate through the shared lobby interface.
    friend class LobbyStore;
    friend class LobbyTestAccess;
    [[nodiscard]] virtual LobbyUpdate normalizeUpdate(LobbyUpdate update) const = 0;
    virtual void onUpdate(LobbyUpdate &update, MessageLoader loader = {}) = 0;
    virtual void resetHistoryRecords(std::vector<HistoryRecord> records) = 0;
};