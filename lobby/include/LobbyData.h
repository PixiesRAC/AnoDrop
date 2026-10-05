#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#pragma once

#include <openssl/crypto.h>

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

struct AckState
{
    std::uint64_t through = 0;
    std::vector<std::uint64_t> selective; // Bounded, sorted, no duplicates.

    bool operator==(const AckState &) const = default;

    [[nodiscard]] bool acknowledges(std::uint64_t sequence) const
    {
        return sequence != 0 &&
               (sequence <= through ||
                std::binary_search(selective.begin(), selective.end(), sequence));
    }

    void compress()
    {
        std::sort(selective.begin(), selective.end());
        auto last = std::unique(selective.begin(), selective.end());
        selective.erase(last, selective.end());

        while (!selective.empty() && selective.front() == through + 1)
        {
            through++;
            selective.erase(selective.begin());
        }
    }
};

struct LobbyData
{
    std::string lobby_name;
    std::string my_pseudo;
    std::string my_x25519_pub;
    std::string my_x25519_priv; // Needs secure zeroing
    std::string other_pseudo;
    std::string announced_x25519_pub;
    std::string private_node_discussion; // Target root for DHT messages, needs zeroing

    LobbyData() = default;

    LobbyData(const LobbyData &other) = default;
    LobbyData &operator=(const LobbyData &other)
    {
        if (this != &other)
        {
            if (!my_x25519_priv.empty())
                OPENSSL_cleanse(my_x25519_priv.data(), my_x25519_priv.capacity());
            if (!private_node_discussion.empty())
                OPENSSL_cleanse(private_node_discussion.data(), private_node_discussion.capacity());

            lobby_name = other.lobby_name;
            my_pseudo = other.my_pseudo;
            my_x25519_pub = other.my_x25519_pub;
            my_x25519_priv = other.my_x25519_priv;
            other_pseudo = other.other_pseudo;
            announced_x25519_pub = other.announced_x25519_pub;
            private_node_discussion = other.private_node_discussion;
            status = other.status;
            my_seq_num = other.my_seq_num;
            announced_seq_num = other.announced_seq_num;
            dht_acked_seq_nums = other.dht_acked_seq_nums;
            peer_acked_seq_nums = other.peer_acked_seq_nums;
            downloaded_seq_nums = other.downloaded_seq_nums;
            resynced_seq_nums = other.resynced_seq_nums;
            fake_timezone = other.fake_timezone;
            history = other.history;
            read_messages_count = other.read_messages_count;
            is_hidden = other.is_hidden;
            anomaly_message = other.anomaly_message;
            last_heartbeat = other.last_heartbeat;
        }
        return *this;
    }

    LobbyData(LobbyData &&other) noexcept
        : lobby_name(std::move(other.lobby_name)), my_pseudo(std::move(other.my_pseudo)), my_x25519_pub(std::move(other.my_x25519_pub)), my_x25519_priv(std::exchange(other.my_x25519_priv, "")), other_pseudo(std::move(other.other_pseudo)), announced_x25519_pub(std::move(other.announced_x25519_pub)), private_node_discussion(std::exchange(other.private_node_discussion, "")), status(other.status), my_seq_num(other.my_seq_num), announced_seq_num(other.announced_seq_num), dht_acked_seq_nums(std::move(other.dht_acked_seq_nums)), peer_acked_seq_nums(std::move(other.peer_acked_seq_nums)), downloaded_seq_nums(std::move(other.downloaded_seq_nums)), resynced_seq_nums(std::move(other.resynced_seq_nums)), fake_timezone(other.fake_timezone), history(std::move(other.history)), read_messages_count(other.read_messages_count), is_hidden(other.is_hidden), anomaly_message(std::move(other.anomaly_message)), last_heartbeat(other.last_heartbeat)
    {
    }

    LobbyData &operator=(LobbyData &&other) noexcept
    {
        if (this != &other)
        {
            if (!my_x25519_priv.empty())
                OPENSSL_cleanse(my_x25519_priv.data(), my_x25519_priv.capacity());
            if (!private_node_discussion.empty())
                OPENSSL_cleanse(private_node_discussion.data(), private_node_discussion.capacity());

            lobby_name = std::move(other.lobby_name);
            my_pseudo = std::move(other.my_pseudo);
            my_x25519_pub = std::move(other.my_x25519_pub);
            my_x25519_priv = std::exchange(other.my_x25519_priv, "");
            other_pseudo = std::move(other.other_pseudo);
            announced_x25519_pub = std::move(other.announced_x25519_pub);
            private_node_discussion = std::exchange(other.private_node_discussion, "");
            status = other.status;
            my_seq_num = other.my_seq_num;
            announced_seq_num = other.announced_seq_num;
            dht_acked_seq_nums = std::move(other.dht_acked_seq_nums);
            peer_acked_seq_nums = std::move(other.peer_acked_seq_nums);
            downloaded_seq_nums = std::move(other.downloaded_seq_nums);
            resynced_seq_nums = std::move(other.resynced_seq_nums);
            fake_timezone = other.fake_timezone;
            history = std::move(other.history);
            read_messages_count = other.read_messages_count;
            is_hidden = other.is_hidden;
            anomaly_message = std::move(other.anomaly_message);
            last_heartbeat = other.last_heartbeat;
        }
        return *this;
    }

    ~LobbyData()
    {
        if (!my_x25519_priv.empty())
        {
            OPENSSL_cleanse(my_x25519_priv.data(), my_x25519_priv.capacity());
        }
        if (!private_node_discussion.empty())
        {
            OPENSSL_cleanse(private_node_discussion.data(), private_node_discussion.capacity());
        }
    }

    LobbyStatus status;
    std::int64_t my_seq_num = 0;
    std::int64_t announced_seq_num = 0;
    AckState dht_acked_seq_nums;                 // Confirmed outgoing seqNums (by DHT)
    AckState peer_acked_seq_nums;                // Confirmed outgoing seqNums (by peer)
    AckState downloaded_seq_nums;                // Confirmed incoming seqNums (by us, used for O(1) gap detection)
    std::vector<std::int64_t> resynced_seq_nums; // Sorted vector of seqNums that were resynced
    FakeTimezone fake_timezone;
    LobbyHistoricalData history;
    std::size_t read_messages_count = 0;
    bool is_hidden = false;
    std::string anomaly_message = "";
    int64_t last_heartbeat = 0;

    bool isEmpty() const
    {
        return lobby_name.empty();
    }

    bool isPeerConnected() const
    {
        if (last_heartbeat <= 0)
            return false;
        int64_t currentFakeTime = TimeUtils::computeFakeTimestamp(fake_timezone.offset_seconds);
        return (currentFakeTime - last_heartbeat) <= 300; // 300s = 5 minutes timeout
    }

    bool operator==(const LobbyData &) const = default;
};

struct PeerInfo
{
    std::string pseudo;
    std::string x25519_pub;
    FakeTimezone fake_timezone;
};
