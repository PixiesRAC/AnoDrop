#pragma once

#include "FakeTimezonePool.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <openssl/crypto.h>

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
    bool operator==(const ChatMessage&) const = default;
};

inline void to_json(nlohmann::json &j, const ChatMessage &m)
{
    j = nlohmann::json{{"direction", m.direction}, {"content", m.content}, {"fake_timestamp", m.fake_timestamp}, {"seq_num", m.seq_num}};
}

inline void from_json(const nlohmann::json &j, ChatMessage &m)
{
    m.direction = j.value("direction", MessageDirection::OUT);
    m.content = j.value("content", "");
    m.fake_timestamp = j.value("fake_timestamp", static_cast<int64_t>(0));
    m.seq_num = j.value("seq_num", static_cast<int64_t>(0));
}

struct LobbyHistoricalData
{
    std::vector<ChatMessage> messages;
    bool operator==(const LobbyHistoricalData&) const = default;
};

inline void to_json(nlohmann::json &j, const LobbyHistoricalData &h)
{
    j = nlohmann::json{{"messages", h.messages}};
}

inline void from_json(const nlohmann::json &j, LobbyHistoricalData &h)
{
    if (j.contains("messages"))
    {
        h.messages = j.at("messages").get<std::vector<ChatMessage>>();
    }
}

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
    LobbyData(const LobbyData& other) = default;
    LobbyData(LobbyData&& other) = default;
    LobbyData& operator=(const LobbyData& other) = default;
    LobbyData& operator=(LobbyData&& other) = default;

    ~LobbyData() {
        if (!my_x25519_priv.empty()) {
            OPENSSL_cleanse(my_x25519_priv.data(), my_x25519_priv.capacity());
        }
        if (!private_node_discussion.empty()) {
            OPENSSL_cleanse(private_node_discussion.data(), private_node_discussion.capacity());
        }
    }

    LobbyStatus status;
    std::int64_t my_seq_num = 0;
    std::int64_t announced_seq_num = 0;
    std::vector<std::int64_t> acked_seq_nums; // Sorted vector of confirmed outgoing seqNums
    std::vector<std::int64_t> resynced_seq_nums; // Sorted vector of seqNums that were resynced
    FakeTimezone fake_timezone;
    LobbyHistoricalData history;
    std::size_t read_messages_count = 0;
    bool is_hidden = false;
    std::string anomaly_message = "";

    bool isEmpty() const
    {
        return my_x25519_pub.empty() && my_x25519_priv.empty();
    }
    bool operator==(const LobbyData&) const = default;
};

inline void to_json(nlohmann::json &j, const LobbyData &l)
{
    j = nlohmann::json{{"lobby_name", l.lobby_name}, {"my_pseudo", l.my_pseudo}, {"my_x25519_pub", l.my_x25519_pub}, {"my_x25519_priv", l.my_x25519_priv}, {"other_pseudo", l.other_pseudo}, {"announced_x25519_pub", l.announced_x25519_pub}, {"private_node_discussion", l.private_node_discussion}, {"status", l.status}, {"my_seq_num", l.my_seq_num}, {"announced_seq_num", l.announced_seq_num}, {"acked_seq_nums", l.acked_seq_nums}, {"resynced_seq_nums", l.resynced_seq_nums}, {"fake_timezone", l.fake_timezone}, {"history", l.history}, {"read_messages_count", l.read_messages_count}, {"is_hidden", l.is_hidden}, {"anomaly_message", l.anomaly_message}};
}

inline void from_json(const nlohmann::json &j, LobbyData &l)
{
    l.lobby_name = j.value("lobby_name", "");
    l.my_pseudo = j.value("my_pseudo", "");
    l.my_x25519_pub = j.value("my_x25519_pub", "");
    l.my_x25519_priv = j.value("my_x25519_priv", "");
    l.other_pseudo = j.value("other_pseudo", "");
    l.announced_x25519_pub = j.value("announced_x25519_pub", "");
    l.private_node_discussion = j.value("private_node_discussion", "");
    l.status = j.value("status", LobbyStatus::PENDING_HANDSHAKE);
    l.my_seq_num = j.value("my_seq_num", static_cast<int64_t>(0));
    l.announced_seq_num = j.value("announced_seq_num", static_cast<int64_t>(0));
    if (j.contains("acked_seq_nums"))
    {
        l.acked_seq_nums = j.at("acked_seq_nums").get<std::vector<std::int64_t>>();
    }
    if (j.contains("resynced_seq_nums"))
    {
        l.resynced_seq_nums = j.at("resynced_seq_nums").get<std::vector<std::int64_t>>();
    }

    if (j.contains("fake_timezone"))
    {
        l.fake_timezone = j.at("fake_timezone").get<FakeTimezone>();
    }
    if (j.contains("history"))
    {
        l.history = j.at("history").get<LobbyHistoricalData>();
    }
    l.read_messages_count = j.value("read_messages_count", static_cast<std::size_t>(0));
    l.is_hidden = j.value("is_hidden", false);
    l.anomaly_message = j.value("anomaly_message", "");
}

struct PeerInfo
{
    std::string pseudo;
    std::string x25519_pub;
    FakeTimezone fake_timezone;
};

inline void to_json(nlohmann::json &j, const PeerInfo &p)
{
    j = nlohmann::json{{"pseudo", p.pseudo}, {"x25519_pub", p.x25519_pub}, {"fake_timezone", p.fake_timezone}};
}

inline void from_json(const nlohmann::json &j, PeerInfo &p)
{
    p.pseudo = j.value("pseudo", "");
    p.x25519_pub = j.value("x25519_pub", "");
    if (j.contains("fake_timezone"))
    {
        p.fake_timezone = j.at("fake_timezone").get<FakeTimezone>();
    }
}
