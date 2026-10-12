#pragma once

#include <functional>
#include <span>
#include <string>
#include <nlohmann/json.hpp>

#include "LobbyData.h"

class LobbyObserver
{
  public:
    LobbyObserver() = default;
    virtual ~LobbyObserver() = default;

    virtual void onUpdate(nlohmann::json &jsonPatch) = 0;
    [[nodiscard]] virtual std::string getLobbyName() const = 0;
    [[nodiscard]] virtual LobbyData getLobbyData() const = 0;
    [[nodiscard]] virtual LobbyData getLobbyDataWithoutHistory() const = 0;
    [[nodiscard]] virtual LobbyMetadata getMetadata() const = 0;
    [[nodiscard]] virtual LobbyStatus getStatus() const = 0;
    [[nodiscard]] virtual bool isPeerConnected() const = 0;
    [[nodiscard]] virtual LobbyNetworkState getNetworkState() const = 0;
    [[nodiscard]] virtual LobbyCryptoVault getCryptoVault() const = 0;

    using HistoryReader = std::function<void(std::span<const ChatMessage>)>;
    virtual void withHistory(HistoryReader reader) const = 0;

    using DataViewReader = std::function<void(const LobbyMetadata &, const LobbySyncState &, const LobbyCryptoVault &)>;
    virtual void withDataView(DataViewReader reader) const = 0;

    virtual void setFetchingSeq() = 0;
    virtual void clearFetchingSeq() = 0;
    [[nodiscard]] virtual bool isFetchingSeq() const = 0;
    [[nodiscard]] virtual bool consumeUpdateFlag() = 0;
    [[nodiscard]] virtual bool hasUnreadMessages() const { return false; }
};