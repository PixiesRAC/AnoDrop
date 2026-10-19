#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "FakeTimezonePool.h"
#include "SecureBuffer.h"
#include "TimeUtils.h"

enum class LobbyStatus
{
    PENDING_HANDSHAKE = 0,
    HANDSHAKE_DONE = 1,
    ANOMALY = 2,
    CONFIRMATION_PENDING = 3
};

enum class MessageDirection
{
    OUT = 0,
    IN = 1
};

struct ChatMessage
{
    MessageDirection direction;
    // Uses SecureString so that decrypted message text is wiped from RAM on destruction (OPENSSL_cleanse).
    // Note: In a future security hardening phase, OPENSSL_secure_malloc / mlock should be integrated
    // to prevent sensitive plaintext in RAM from being swapped to disk by the kernel.
    SecureString content;
    int64_t fake_timestamp; // UTC epoch seconds + lobby fake timezone offset
    int64_t seq_num = 0;    // Sequence number of this message in the DHT
    bool operator==(const ChatMessage &) const = default;
};

using MessageLoader = std::function<ChatMessage()>;
struct HistoryRecord
{
    ChatMessage metadata;
    MessageLoader load;
    [[nodiscard]] ChatMessage read() const
    {
        return load ? load() : metadata;
    }
};
struct LobbyHistoricalData
{
    std::vector<HistoryRecord> records;
    void append(ChatMessage message, MessageLoader loader = {})
    {
        if (loader)
            message.content.clear();
        records.push_back({std::move(message), std::move(loader)});
    }
    [[nodiscard]] std::vector<ChatMessage> materialize() const
    {
        std::vector<ChatMessage> result;
        result.reserve(records.size());
        for (const auto &record : records)
            result.push_back(record.read());
        return result;
    }
    bool operator==(const LobbyHistoricalData &other) const
    {
        if (records.size() != other.records.size())
            return false;
        return materialize() == other.materialize();
    }
};


struct AckState
{
    std::uint64_t through = 0;
    std::vector<std::uint64_t> selective; // Bounded, sorted, no duplicates.

    bool operator==(const AckState &) const = default;

    [[nodiscard]] bool acknowledges(std::uint64_t sequence) const
    {
        if (sequence == 0)
            return true;
        return (sequence <= through ||
                std::binary_search(selective.begin(), selective.end(), sequence));
    }

    void compress()
    {
        std::erase_if(selective, [this](std::uint64_t s)
                      { return s <= through; });

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
    std::string handshake_error;
    int64_t last_heartbeat = 0;
    FakeTimezone fake_timezone;

    [[nodiscard]] bool isPeerConnected() const
    {
        if (last_heartbeat <= 0)
            return false;
        int64_t currentFakeTime = TimeUtils::computeFakeTimestamp(fake_timezone.offset_seconds);
        return (currentFakeTime - last_heartbeat) <= TimeUtils::HEARTBEAT_TIMEOUT_SECONDS;
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
    LobbyCryptoVault(const LobbyCryptoVault &) = delete;
    LobbyCryptoVault &operator=(const LobbyCryptoVault &) = delete;
    LobbyCryptoVault(LobbyCryptoVault &&) noexcept = default;
    LobbyCryptoVault &operator=(LobbyCryptoVault &&) noexcept = default;
    ~LobbyCryptoVault() = default;

    bool operator==(const LobbyCryptoVault &) const = default;
};


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


struct LobbyViewData
{
    LobbyMetadata metadata;
    LobbySyncState sync;
};

struct LobbyData
{
    LobbyMetadata metadata;
    LobbySyncState sync;
    std::shared_ptr<const LobbyCryptoVault> crypto{std::make_shared<LobbyCryptoVault>()};
    LobbyHistoricalData history;

    LobbyData() = default;
    LobbyData(LobbyMetadata meta, LobbySyncState s, std::shared_ptr<const LobbyCryptoVault> c, LobbyHistoricalData h = {})
        : metadata(std::move(meta)), sync(std::move(s)), crypto(std::move(c)), history(std::move(h)) {}

    [[nodiscard]] bool isPeerConnected() const noexcept
    {
        return metadata.isPeerConnected();
    }
    [[nodiscard]] LobbyData cloneWithoutHistory() const
    {
        return LobbyData{metadata, sync, crypto, {}};
    }

    bool operator==(const LobbyData &other) const
    {
        const bool cryptoEqual = (!crypto && !other.crypto) ||
                                 (crypto && other.crypto && *crypto == *other.crypto);
        return metadata == other.metadata && sync == other.sync &&
               cryptoEqual && history == other.history;
    }
};

struct PeerInfo
{
    std::string pseudo;
    std::string x25519_pub;
    FakeTimezone fake_timezone;
    std::string confirmed_peer;
    int protocol_version{2};
};
