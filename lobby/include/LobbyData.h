#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "SecureBuffer.h"
#include "FakeTimezonePool.h"
#include "TimeUtils.h"

enum class LobbyStatus
{
    PENDING_HANDSHAKE = 0,
    HANDSHAKE_DONE = 1,
    ANOMALY = 2
};

enum class MessageDirection
{
    OUT = 0,
    IN = 1
};

struct ChatMessage
{
    MessageDirection direction;
    std::string content;
    int64_t fake_timestamp; // UTC epoch seconds + lobby fake timezone offset
    int64_t seq_num = 0;    // Sequence number of this message in the DHT
    bool operator==(const ChatMessage &) const = default;
};

struct LobbyHistoricalData
{
    std::vector<ChatMessage> messages;

    LobbyHistoricalData() = default;

    bool operator==(const LobbyHistoricalData &other) const
    {
        return messages == other.messages;
    }
};

using MessageHistory = LobbyHistoricalData;

struct AckState
{
    std::uint64_t through = 0;
    std::vector<std::uint64_t> selective; // Bounded, sorted, no duplicates.

    bool operator==(const AckState &) const = default;

    [[nodiscard]] bool acknowledges(std::uint64_t sequence) const
    {
        if (sequence == 0) return true;
        return (sequence <= through ||
                std::binary_search(selective.begin(), selective.end(), sequence));
    }

    void compress()
    {
        auto it = std::remove_if(selective.begin(), selective.end(), [this](std::uint64_t s) { return s <= through; });
        selective.erase(it, selective.end());

        std::sort(selective.begin(), selective.end());
        auto last = std::unique(selective.begin(), selective.end());
        selective.erase(last, selective.end());

        auto curr = selective.begin();
        while (curr != selective.end() && *curr == through + 1)
        {
            through++;
            ++curr;
        }
        selective.erase(selective.begin(), curr);
    }
};

struct LobbyMetadata
{
    std::string lobby_name;
    std::string my_pseudo;
    std::string other_pseudo;
    LobbyStatus status = LobbyStatus::PENDING_HANDSHAKE;
    std::size_t read_messages_count = 0;
    bool is_hidden = false;
    std::string anomaly_message = "";
    int64_t last_heartbeat = 0;
    FakeTimezone fake_timezone;

    [[nodiscard]] bool isEmpty() const
    {
        return lobby_name.empty();
    }

    [[nodiscard]] bool isPeerConnected() const
    {
        if (last_heartbeat <= 0)
            return false;
        int64_t currentFakeTime = TimeUtils::computeFakeTimestamp(fake_timezone.offset_seconds);
        return (currentFakeTime - last_heartbeat) <= 300; // 300s = 5 minutes timeout
    }

    bool operator==(const LobbyMetadata &) const = default;
};

struct LobbyCryptoVault
{
    std::string my_x25519_pub;
    SecureString my_x25519_priv; // RAII auto-cleansed
    std::string announced_x25519_pub;
    SecureString private_node_discussion; // Target root for DHT messages, RAII auto-cleansed

    LobbyCryptoVault() = default;
    LobbyCryptoVault(const LobbyCryptoVault &) = default;
    LobbyCryptoVault &operator=(const LobbyCryptoVault &) = default;
    LobbyCryptoVault(LobbyCryptoVault &&) noexcept = default;
    LobbyCryptoVault &operator=(LobbyCryptoVault &&) noexcept = default;
    ~LobbyCryptoVault() = default;

    void secureCleanse()
    {
        my_x25519_priv.cleanse();
        private_node_discussion.cleanse();
    }

    bool operator==(const LobbyCryptoVault &) const = default;
};

using LobbyCryptoState = LobbyCryptoVault;

struct LobbySyncState
{
    std::int64_t my_seq_num = 0;
    std::int64_t announced_seq_num = 0;
    AckState dht_acked_seq_nums;                 // Confirmed outgoing seqNums (by DHT)
    AckState peer_acked_seq_nums;                // Confirmed outgoing seqNums (by peer)
    AckState downloaded_seq_nums;                // Confirmed incoming seqNums (by us, used for O(1) gap detection)
    std::vector<std::int64_t> resynced_seq_nums; // Sorted vector of seqNums that were resynced

    bool operator==(const LobbySyncState &) const = default;
};

using LobbyNetworkState = LobbySyncState;

struct LobbyData
{
    LobbyMetadata metadata;
    LobbySyncState sync;
    LobbyCryptoVault crypto;
    MessageHistory history;

    // Direct convenience references to composed fields for legacy and contract compatibility
    std::string &lobby_name{metadata.lobby_name};
    std::string &my_pseudo{metadata.my_pseudo};
    std::string &other_pseudo{metadata.other_pseudo};
    LobbyStatus &status{metadata.status};
    std::size_t &read_messages_count{metadata.read_messages_count};
    bool &is_hidden{metadata.is_hidden};
    std::string &anomaly_message{metadata.anomaly_message};
    int64_t &last_heartbeat{metadata.last_heartbeat};
    FakeTimezone &fake_timezone{metadata.fake_timezone};

    std::string &my_x25519_pub{crypto.my_x25519_pub};
    SecureString &my_x25519_priv{crypto.my_x25519_priv};
    std::string &announced_x25519_pub{crypto.announced_x25519_pub};
    SecureString &private_node_discussion{crypto.private_node_discussion};

    std::int64_t &my_seq_num{sync.my_seq_num};
    std::int64_t &announced_seq_num{sync.announced_seq_num};
    AckState &dht_acked_seq_nums{sync.dht_acked_seq_nums};
    AckState &peer_acked_seq_nums{sync.peer_acked_seq_nums};
    AckState &downloaded_seq_nums{sync.downloaded_seq_nums};
    std::vector<std::int64_t> &resynced_seq_nums{sync.resynced_seq_nums};

    LobbyData() = default;

    LobbyData(const LobbyData &other)
        : metadata(other.metadata), sync(other.sync), crypto(other.crypto), history(other.history)
    {
    }

    LobbyData(LobbyData &&other) noexcept
        : metadata(std::move(other.metadata)), sync(std::move(other.sync)),
          crypto(std::move(other.crypto)), history(std::move(other.history))
    {
    }

    LobbyData &operator=(const LobbyData &other)
    {
        if (this != &other)
        {
            metadata = other.metadata;
            sync = other.sync;
            crypto = other.crypto;
            history = other.history;
        }
        return *this;
    }

    LobbyData &operator=(LobbyData &&other) noexcept
    {
        if (this != &other)
        {
            metadata = std::move(other.metadata);
            sync = std::move(other.sync);
            crypto = std::move(other.crypto);
            history = std::move(other.history);
        }
        return *this;
    }

    ~LobbyData() = default;

    [[nodiscard]] bool isHandshakeDone() const noexcept { return metadata.status == LobbyStatus::HANDSHAKE_DONE; }
    [[nodiscard]] bool isAnomaly() const noexcept { return metadata.status == LobbyStatus::ANOMALY; }
    [[nodiscard]] bool isPendingHandshake() const noexcept { return metadata.status == LobbyStatus::PENDING_HANDSHAKE; }
    [[nodiscard]] bool isPeerConnected() const noexcept { return metadata.isPeerConnected(); }
    [[nodiscard]] bool isEmpty() const noexcept { return metadata.isEmpty(); }

    [[nodiscard]] const LobbyMetadata &getMetadata() const noexcept { return metadata; }
    [[nodiscard]] LobbyMetadata &getMetadata() noexcept { return metadata; }

    [[nodiscard]] const LobbySyncState &getSyncState() const noexcept { return sync; }
    [[nodiscard]] LobbySyncState &getSyncState() noexcept { return sync; }
    [[nodiscard]] const LobbyNetworkState &getNetworkState() const noexcept { return sync; }
    [[nodiscard]] LobbyNetworkState &getNetworkState() noexcept { return sync; }

    [[nodiscard]] const LobbyCryptoVault &getCryptoVault() const noexcept { return crypto; }
    [[nodiscard]] LobbyCryptoVault &getCryptoVault() noexcept { return crypto; }
    [[nodiscard]] const LobbyCryptoState &getCryptoState() const noexcept { return crypto; }
    [[nodiscard]] LobbyCryptoState &getCryptoState() noexcept { return crypto; }

    [[nodiscard]] const MessageHistory &getHistory() const noexcept { return history; }
    [[nodiscard]] MessageHistory &getHistory() noexcept { return history; }

    [[nodiscard]] LobbyData cloneWithoutHistory() const
    {
        LobbyData copy;
        copy.metadata = metadata;
        copy.sync = sync;
        copy.crypto = crypto;
        return copy;
    }

    bool operator==(const LobbyData &other) const
    {
        return metadata == other.metadata && sync == other.sync &&
               crypto == other.crypto && history == other.history;
    }
};

struct PeerInfo
{
    std::string pseudo;
    std::string x25519_pub;
    FakeTimezone fake_timezone;
};
