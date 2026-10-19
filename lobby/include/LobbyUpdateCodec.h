#pragma once
#include <stdexcept>

#include "LobbyDataSerializer.h"
#include "LobbyUpdate.h"

inline void to_json(nlohmann::json &j, const LobbyUpdate &u)
{
    j = nlohmann::json::object();
    if (u.handshake_error)
        j["handshake_error"] = *u.handshake_error;
    if (u.status)
        j["status"] = *u.status;
    if (u.my_pseudo)
        j["my_pseudo"] = *u.my_pseudo;
    if (u.other_pseudo)
        j["other_pseudo"] = *u.other_pseudo;
    if (u.my_x25519_pub)
        j["my_x25519_pub"] = *u.my_x25519_pub;
    if (u.my_x25519_priv)
        j["my_x25519_priv"] = *u.my_x25519_priv;
    if (u.announced_x25519_pub)
        j["announced_x25519_pub"] = *u.announced_x25519_pub;
    if (u.private_node_discussion)
        j["private_node_discussion"] = *u.private_node_discussion;
    if (u.my_seq_num)
        j["my_seq_num"] = *u.my_seq_num;
    if (u.announced_seq_num)
        j["announced_seq_num"] = *u.announced_seq_num;
    if (u.dht_acked_seq_nums)
        j["dht_acked_seq_nums"] = *u.dht_acked_seq_nums;
    if (u.peer_acked_seq_nums)
        j["peer_acked_seq_nums"] = *u.peer_acked_seq_nums;
    if (u.resynced_seq_nums)
        j["resynced_seq_nums"] = *u.resynced_seq_nums;
    if (u.fake_timezone)
        j["fake_timezone"] = *u.fake_timezone;
    if (u.read_messages_count)
        j["read_messages_count"] = *u.read_messages_count;
    if (u.is_hidden)
        j["is_hidden"] = *u.is_hidden;
    if (u.anomaly_message)
        j["anomaly_message"] = *u.anomaly_message;
    if (u.last_heartbeat)
        j["last_heartbeat"] = *u.last_heartbeat;
    if (u.new_message)
        j["new_message"] = *u.new_message;
    if (u.message_sent_confirm)
        j["message_sent_confirm"] = *u.message_sent_confirm;
}
inline void from_json(const nlohmann::json &j, LobbyUpdate &u)
{
    if (!j.is_object())
        throw std::invalid_argument("Invalid lobby update");
    u = {};
    if (j.contains("handshake_error"))
        u.handshake_error = j.at("handshake_error").get<std::string>();
    if (j.contains("status"))
        u.status = j.at("status").get<LobbyStatus>();
    if (j.contains("my_pseudo"))
        u.my_pseudo = j.at("my_pseudo").get<std::string>();
    if (j.contains("other_pseudo"))
        u.other_pseudo = j.at("other_pseudo").get<std::string>();
    if (j.contains("my_x25519_pub"))
        u.my_x25519_pub = j.at("my_x25519_pub").get<std::string>();
    if (j.contains("my_x25519_priv"))
        u.my_x25519_priv = j.at("my_x25519_priv").get<SecureString>();
    if (j.contains("announced_x25519_pub"))
        u.announced_x25519_pub = j.at("announced_x25519_pub").get<std::string>();
    if (j.contains("private_node_discussion"))
        u.private_node_discussion = j.at("private_node_discussion").get<SecureString>();
    if (j.contains("my_seq_num"))
        u.my_seq_num = j.at("my_seq_num").get<std::int64_t>();
    if (j.contains("announced_seq_num"))
        u.announced_seq_num = j.at("announced_seq_num").get<std::int64_t>();
    if (j.contains("dht_acked_seq_nums"))
        u.dht_acked_seq_nums = j.at("dht_acked_seq_nums").get<AckState>();
    if (j.contains("peer_acked_seq_nums"))
        u.peer_acked_seq_nums = j.at("peer_acked_seq_nums").get<AckState>();
    if (j.contains("resynced_seq_nums"))
        u.resynced_seq_nums = j.at("resynced_seq_nums").get<std::vector<std::int64_t>>();
    if (j.contains("fake_timezone"))
        u.fake_timezone = j.at("fake_timezone").get<FakeTimezone>();
    if (j.contains("read_messages_count"))
        u.read_messages_count = j.at("read_messages_count").get<std::size_t>();
    if (j.contains("is_hidden"))
        u.is_hidden = j.at("is_hidden").get<bool>();
    if (j.contains("anomaly_message"))
        u.anomaly_message = j.at("anomaly_message").get<std::string>();
    if (j.contains("last_heartbeat"))
        u.last_heartbeat = j.at("last_heartbeat").get<std::int64_t>();
    if (j.contains("new_message"))
        u.new_message = j.at("new_message").get<ChatMessage>();
    if (j.contains("message_sent_confirm"))
        u.message_sent_confirm = j.at("message_sent_confirm").get<std::int64_t>();
}
