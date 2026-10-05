#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"

void to_json(nlohmann::json &j, const FakeTimezone &tz)
{
    j = nlohmann::json{{"name", tz.name}, {"offset_seconds", tz.offset_seconds}};
}

void from_json(const nlohmann::json &j, FakeTimezone &tz)
{
    tz.name = j.value("name", "");
    tz.offset_seconds = j.value("offset_seconds", 0);
}
void to_json(nlohmann::json &j, const ChatMessage &m)
{
    j = nlohmann::json{{"direction", m.direction}, {"content", m.content}, {"fake_timestamp", m.fake_timestamp}, {"seq_num", m.seq_num}};
}

void from_json(const nlohmann::json &j, ChatMessage &m)
{
    m.direction = j.value("direction", MessageDirection::OUT);
    m.content = j.value("content", "");
    m.fake_timestamp = j.value("fake_timestamp", static_cast<int64_t>(0));
    m.seq_num = j.value("seq_num", static_cast<int64_t>(0));
}

void to_json(nlohmann::json &j, const LobbyHistoricalData &h)
{
    j = nlohmann::json{{"messages", h.messages}};
}

void from_json(const nlohmann::json &j, LobbyHistoricalData &h)
{
    if (j.contains("messages"))
    {
        h.messages = j.at("messages").get<std::vector<ChatMessage>>();
    }
    else
    {
        h.messages.clear();
    }
}

void to_json(nlohmann::json &j, const AckState &a)
{
    j = nlohmann::json{{"through", a.through}, {"selective", a.selective}};
}

void from_json(const nlohmann::json &j, AckState &a)
{
    a.through = j.value("through", static_cast<std::uint64_t>(0));
    if (j.contains("selective"))
    {
        a.selective = j.at("selective").get<std::vector<std::uint64_t>>();
    }
}

void to_json(nlohmann::json &j, const LobbyData &l)
{
    j = nlohmann::json{
        {"lobby_name", l.lobby_name},
        {"my_pseudo", l.my_pseudo},
        {"other_pseudo", l.other_pseudo},
        {"status", l.status},
        {"is_hidden", l.is_hidden},
        {"anomaly_message", l.anomaly_message},
        {"last_heartbeat", l.last_heartbeat},
        {"fake_timezone", l.fake_timezone},
        {"history", l.history},
        {"read_messages_count", l.read_messages_count},
        {"keys", {
            {"my_x25519_pub", l.my_x25519_pub},
            {"my_x25519_priv", l.my_x25519_priv},
            {"announced_x25519_pub", l.announced_x25519_pub},
            {"private_node_discussion", l.private_node_discussion}
        }},
        {"outbound", {
            {"my_seq_num", l.my_seq_num},
            {"dht_acked_seq_nums", l.dht_acked_seq_nums},
            {"peer_acked_seq_nums", l.peer_acked_seq_nums}
        }},
        {"inbound", {
            {"announced_seq_num", l.announced_seq_num},
            {"downloaded_seq_nums", l.downloaded_seq_nums},
            {"resynced_seq_nums", l.resynced_seq_nums}
        }}
    };
}

void from_json(const nlohmann::json &j, LobbyData &l)
{
    l.lobby_name = j.value("lobby_name", "");
    l.my_pseudo = j.value("my_pseudo", "");
    l.other_pseudo = j.value("other_pseudo", "");
    l.status = j.value("status", LobbyStatus::PENDING_HANDSHAKE);
    l.is_hidden = j.value("is_hidden", false);
    l.anomaly_message = j.value("anomaly_message", "");
    l.last_heartbeat = j.value("last_heartbeat", static_cast<int64_t>(0));
    if (j.contains("fake_timezone")) l.fake_timezone = j.at("fake_timezone").get<FakeTimezone>();
    if (j.contains("history")) l.history = j.at("history").get<LobbyHistoricalData>();
    l.read_messages_count = j.value("read_messages_count", static_cast<std::size_t>(0));

    if (j.contains("keys")) {
        auto& k = j["keys"];
        l.my_x25519_pub = k.value("my_x25519_pub", "");
        l.my_x25519_priv = k.value("my_x25519_priv", "");
        l.announced_x25519_pub = k.value("announced_x25519_pub", "");
        l.private_node_discussion = k.value("private_node_discussion", "");
    } else {
        l.my_x25519_pub = j.value("my_x25519_pub", "");
        l.my_x25519_priv = j.value("my_x25519_priv", "");
        l.announced_x25519_pub = j.value("announced_x25519_pub", "");
        l.private_node_discussion = j.value("private_node_discussion", "");
    }

    if (j.contains("outbound")) {
        auto& o = j["outbound"];
        l.my_seq_num = o.value("my_seq_num", static_cast<int64_t>(0));
        if (o.contains("dht_acked_seq_nums")) l.dht_acked_seq_nums = o.at("dht_acked_seq_nums").get<AckState>();
        if (o.contains("peer_acked_seq_nums")) l.peer_acked_seq_nums = o.at("peer_acked_seq_nums").get<AckState>();
    } else {
        l.my_seq_num = j.value("my_seq_num", static_cast<int64_t>(0));
        if (j.contains("dht_acked_seq_nums")) l.dht_acked_seq_nums = j.at("dht_acked_seq_nums").get<AckState>();
        if (j.contains("peer_acked_seq_nums")) l.peer_acked_seq_nums = j.at("peer_acked_seq_nums").get<AckState>();
    }

    if (j.contains("inbound")) {
        auto& i = j["inbound"];
        l.announced_seq_num = i.value("announced_seq_num", static_cast<int64_t>(0));
        if (i.contains("downloaded_seq_nums")) l.downloaded_seq_nums = i.at("downloaded_seq_nums").get<AckState>();
        if (i.contains("resynced_seq_nums")) l.resynced_seq_nums = i.at("resynced_seq_nums").get<std::vector<std::int64_t>>();
    } else {
        l.announced_seq_num = j.value("announced_seq_num", static_cast<int64_t>(0));
        if (j.contains("downloaded_seq_nums")) l.downloaded_seq_nums = j.at("downloaded_seq_nums").get<AckState>();
        if (j.contains("resynced_seq_nums")) l.resynced_seq_nums = j.at("resynced_seq_nums").get<std::vector<std::int64_t>>();
    }
}

void to_json(nlohmann::json &j, const PeerInfo &p)
{
    j = nlohmann::json{{"pseudo", p.pseudo}, {"x25519_pub", p.x25519_pub}, {"fake_timezone", p.fake_timezone}};
}

void from_json(const nlohmann::json &j, PeerInfo &p)
{
    p.pseudo = j.value("pseudo", "");
    p.x25519_pub = j.value("x25519_pub", "");
    if (j.contains("fake_timezone"))
    {
        p.fake_timezone = j.at("fake_timezone").get<FakeTimezone>();
    }
}
