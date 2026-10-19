#pragma once
#include <optional>

#include "LobbyData.h"

// Typed domain update. JSON is used only by persistence and wire codecs.
struct LobbyUpdate
{
    std::optional<LobbyStatus> status;
    std::optional<std::string> my_pseudo;
    std::optional<std::string> other_pseudo;
    std::optional<std::string> my_x25519_pub;
    std::optional<SecureString> my_x25519_priv;
    std::optional<std::string> announced_x25519_pub;
    std::optional<SecureString> private_node_discussion;
    std::optional<std::int64_t> my_seq_num;
    std::optional<std::int64_t> announced_seq_num;
    std::optional<AckState> dht_acked_seq_nums;
    std::optional<AckState> peer_acked_seq_nums;
    std::optional<std::vector<std::int64_t>> resynced_seq_nums;
    std::optional<FakeTimezone> fake_timezone;
    std::optional<std::size_t> read_messages_count;
    std::optional<bool> is_hidden;
    std::optional<std::string> anomaly_message;
    std::optional<std::string> handshake_error;
    std::optional<std::int64_t> last_heartbeat;
    std::optional<ChatMessage> new_message;
    std::optional<std::int64_t> message_sent_confirm;
    [[nodiscard]] bool empty() const
    {
        return !handshake_error && !status && !my_pseudo && !other_pseudo && !my_x25519_pub && !my_x25519_priv && !announced_x25519_pub && !private_node_discussion && !my_seq_num && !announced_seq_num && !dht_acked_seq_nums && !peer_acked_seq_nums && !resynced_seq_nums && !fake_timezone && !read_messages_count && !is_hidden && !anomaly_message && !last_heartbeat && !new_message && !message_sent_confirm;
    }
};
