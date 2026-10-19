#include "LobbyDataSerializer.h"

#include <nlohmann/json.hpp>

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
    j = nlohmann::json{{"messages", h.materialize()}};
}

void from_json(const nlohmann::json &j, LobbyHistoricalData &h)
{
    if (j.contains("messages"))
    {
        h.records.clear();
        for (auto &message : j.at("messages").get<std::vector<ChatMessage>>())
            h.append(std::move(message));
    }
    else
    {
        h.records.clear();
    }
}

void to_json(nlohmann::json &j, const AckState &a)
{
    j = nlohmann::json{{"through", a.through}, {"selective", a.selective}};
}

void from_json(const nlohmann::json &j, AckState &a)
{
    AckState fresh;
    fresh.through = j.value("through", static_cast<std::uint64_t>(0));
    if (j.contains("selective"))
    {
        fresh.selective = j.at("selective").get<std::vector<std::uint64_t>>();
    }
    fresh.compress();
    a = std::move(fresh);
}

void to_json(nlohmann::json &j, const LobbyData &l)
{
    j = nlohmann::json{
        {"lobby_name", l.metadata.lobby_name},
        {"my_pseudo", l.metadata.my_pseudo},
        {"other_pseudo", l.metadata.other_pseudo},
        {"status", l.metadata.status},
        {"is_hidden", l.metadata.is_hidden},
        {"anomaly_message", l.metadata.anomaly_message},
        {"handshake_error", l.metadata.handshake_error},
        {"last_heartbeat", l.metadata.last_heartbeat},
        {"fake_timezone", l.metadata.fake_timezone},
        {"history", l.history},
        {"read_messages_count", l.metadata.read_messages_count},
        {"keys", {{"my_x25519_pub", l.crypto ? l.crypto->my_x25519_pub : ""}, {"my_x25519_priv", l.crypto ? l.crypto->my_x25519_priv : SecureString{}}, {"announced_x25519_pub", l.crypto ? l.crypto->announced_x25519_pub : ""}, {"private_node_discussion", l.crypto ? l.crypto->private_node_discussion : SecureString{}}}},
        {"outbound", {{"my_seq_num", l.sync.my_seq_num}, {"dht_acked_seq_nums", l.sync.dht_acked_seq_nums}, {"peer_acked_seq_nums", l.sync.peer_acked_seq_nums}}},
        {"inbound", {{"announced_seq_num", l.sync.announced_seq_num}, {"downloaded_seq_nums", l.sync.downloaded_seq_nums}, {"resynced_seq_nums", l.sync.resynced_seq_nums}}}};
}

void from_json(const nlohmann::json &j, LobbyData &l)
{
    l.metadata.lobby_name = j.value("lobby_name", "");
    l.metadata.my_pseudo = j.value("my_pseudo", "");
    l.metadata.other_pseudo = j.value("other_pseudo", "");
    l.metadata.status = j.value("status", LobbyStatus::PENDING_HANDSHAKE);
    l.metadata.is_hidden = j.value("is_hidden", false);
    l.metadata.anomaly_message = j.value("anomaly_message", "");
    l.metadata.handshake_error = j.value("handshake_error", "");
    l.metadata.last_heartbeat = j.value("last_heartbeat", static_cast<int64_t>(0));
    if (j.contains("fake_timezone"))
        l.metadata.fake_timezone = j.at("fake_timezone").get<FakeTimezone>();
    if (j.contains("history"))
        l.history = j.at("history").get<LobbyHistoricalData>();
    l.metadata.read_messages_count = j.value("read_messages_count", static_cast<std::size_t>(0));

    auto vault = std::make_shared<LobbyCryptoVault>();
    const auto &keys = j.at("keys");
    keys.at("my_x25519_pub").get_to(vault->my_x25519_pub);
    keys.at("my_x25519_priv").get_to(vault->my_x25519_priv);
    keys.at("announced_x25519_pub").get_to(vault->announced_x25519_pub);
    keys.at("private_node_discussion").get_to(vault->private_node_discussion);
    l.crypto = std::move(vault);

    const auto &outbound = j.at("outbound");
    outbound.at("my_seq_num").get_to(l.sync.my_seq_num);
    outbound.at("dht_acked_seq_nums").get_to(l.sync.dht_acked_seq_nums);
    outbound.at("peer_acked_seq_nums").get_to(l.sync.peer_acked_seq_nums);
    const auto &inbound = j.at("inbound");
    inbound.at("announced_seq_num").get_to(l.sync.announced_seq_num);
    inbound.at("downloaded_seq_nums").get_to(l.sync.downloaded_seq_nums);
    inbound.at("resynced_seq_nums").get_to(l.sync.resynced_seq_nums);
}

void to_json(nlohmann::json &j, const PeerInfo &p)
{
    j = nlohmann::json{{"pseudo", p.pseudo}, {"x25519_pub", p.x25519_pub}, {"fake_timezone", p.fake_timezone}, {"confirmed_peer", p.confirmed_peer}, {"protocol_version", p.protocol_version}};
}

void from_json(const nlohmann::json &j, PeerInfo &p)
{
    p.protocol_version = j.at("protocol_version").get<int>();
    p.confirmed_peer = j.value("confirmed_peer", "");
    p.pseudo = j.value("pseudo", "");
    p.x25519_pub = j.value("x25519_pub", "");
    if (j.contains("fake_timezone"))
    {
        p.fake_timezone = j.at("fake_timezone").get<FakeTimezone>();
    }
}
