#pragma once
#include "LobbyUpdate.h"
#include <stdexcept>
#include <variant>
#include <type_traits>

namespace lobby {
struct ReceiveMessage { ChatMessage message; };
struct StoreOutgoingMessage { ChatMessage message; };
struct ConfirmPublication { std::int64_t sequence; };
struct RecordPeerReceipts { AckState receipts; };
struct AnnounceSequence { std::int64_t sequence; };
struct MarkRead { std::size_t count; };
struct RecordHeartbeat { std::int64_t timestamp; };
struct SetVisibility { bool hidden; };
struct FlagAnomaly { std::string reason; };
struct MarkResynced { std::vector<std::int64_t> sequences; };
struct BeginHandshake { std::string pseudo; };
struct CompleteHandshake {};
struct RecordHandshakeError { std::string reason; };
using Command = std::variant<ReceiveMessage, StoreOutgoingMessage, ConfirmPublication,
    RecordPeerReceipts, AnnounceSequence, MarkRead, RecordHeartbeat, SetVisibility,
    FlagAnomaly, MarkResynced, BeginHandshake, CompleteHandshake, RecordHandshakeError>;

// Translate one intent against the current state, on the repository executor.
// The resulting patch is the persistence format, not the caller's command API.
inline LobbyUpdate prepare(const LobbyData& state, const Command& command) {
    return std::visit([&](const auto& intent) {
        using T = std::decay_t<decltype(intent)>;
        LobbyUpdate update;
        if constexpr (std::is_same_v<T, ReceiveMessage>) {
            if (intent.message.direction != MessageDirection::IN)
                throw std::invalid_argument("Received message must be incoming");
            update.new_message = intent.message;
        } else if constexpr (std::is_same_v<T, StoreOutgoingMessage>) {
            if (intent.message.direction != MessageDirection::OUT || state.sync.my_seq_num == INT64_MAX ||
                intent.message.seq_num != state.sync.my_seq_num + 1)
                throw std::invalid_argument("Outgoing message must allocate the next sequence");
            update.new_message = intent.message;
            update.my_seq_num = intent.message.seq_num;
        } else if constexpr (std::is_same_v<T, ConfirmPublication>) {
            update.message_sent_confirm = intent.sequence;
        } else if constexpr (std::is_same_v<T, RecordPeerReceipts>) {
            update.peer_acked_seq_nums = intent.receipts;
        } else if constexpr (std::is_same_v<T, AnnounceSequence>) {
            update.announced_seq_num = intent.sequence;
        } else if constexpr (std::is_same_v<T, MarkRead>) {
            update.read_messages_count = intent.count;
        } else if constexpr (std::is_same_v<T, RecordHeartbeat>) {
            update.last_heartbeat = intent.timestamp;
        } else if constexpr (std::is_same_v<T, SetVisibility>) {
            update.is_hidden = intent.hidden;
        } else if constexpr (std::is_same_v<T, FlagAnomaly>) {
            update.status = LobbyStatus::ANOMALY;
            update.anomaly_message = intent.reason;
        } else if constexpr (std::is_same_v<T, MarkResynced>) {
            update.resynced_seq_nums = intent.sequences;
        } else if constexpr (std::is_same_v<T, BeginHandshake>) {
            if (!state.metadata.my_pseudo.empty() || state.metadata.status != LobbyStatus::PENDING_HANDSHAKE)
                throw std::invalid_argument("Lobby already initialized");
            update.status = LobbyStatus::PENDING_HANDSHAKE;
            update.my_pseudo = intent.pseudo;
        } else if constexpr (std::is_same_v<T, CompleteHandshake>) {
            if (state.metadata.status == LobbyStatus::CONFIRMATION_PENDING)
                update.status = LobbyStatus::HANDSHAKE_DONE;
        } else if constexpr (std::is_same_v<T, RecordHandshakeError>) {
            update.handshake_error = intent.reason;
        } else {
            static_assert(!sizeof(T), "Unhandled lobby command");
        }
        return update;
    }, command);
}
} // namespace lobby
