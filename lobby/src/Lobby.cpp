#include "Lobby.h"

#include <charconv>
#include <ranges>
#include <spdlog/spdlog.h>

#include "ChatConstants.h"
#include "LobbyUpdate.h"

Lobby::~Lobby() = default;

void Lobby::onUpdate(LobbyUpdate &update, MessageLoader loader)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    update = normalizeUpdateUnlocked(std::move(update));
    if (update.empty())
        return;
    auto next = m_lobbyData.cloneWithoutHistory();
    bool changed = false;

    if (update.new_message.has_value())
    {
        const auto &msg = *update.new_message;

        // normalizeUpdateUnlocked already removed duplicates under this lock.
        changed = true;

        if (msg.direction == MessageDirection::IN)
        {
            if (msg.seq_num > 0 && !next.sync.downloaded_seq_nums.acknowledges(static_cast<std::uint64_t>(msg.seq_num)))
            {
                next.sync.downloaded_seq_nums.selective.push_back(static_cast<std::uint64_t>(msg.seq_num));
            }

            if (msg.content.starts_with(chat::RESYNC_PREFIX))
            {
                std::string_view rest = std::string_view(msg.content).substr(chat::RESYNC_PREFIX.size());
                std::int64_t seq = 0;
                auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), seq);
                if (ec == std::errc() && seq > 0 && !next.sync.downloaded_seq_nums.acknowledges(static_cast<std::uint64_t>(seq)))
                {
                    next.sync.downloaded_seq_nums.selective.push_back(static_cast<std::uint64_t>(seq));
                }
            }

            // Compress the AckState window
            next.sync.downloaded_seq_nums.compress();
        }
        else if (msg.direction == MessageDirection::OUT)
        {
            if (msg.seq_num > next.sync.my_seq_num)
            {
                next.sync.my_seq_num = msg.seq_num;
            }
        }
    }

    if (update.message_sent_confirm.has_value())
    {
        std::int64_t seq = *update.message_sent_confirm;
        if (!next.sync.dht_acked_seq_nums.acknowledges(static_cast<std::uint64_t>(seq)))
        {
            next.sync.dht_acked_seq_nums.selective.push_back(static_cast<std::uint64_t>(seq));
            next.sync.dht_acked_seq_nums.compress();
            changed = true;
        }
    }

    auto assignIfPresent = [&changed](auto &target, const auto &value)
    {
        if (value && target != *value)
        {
            target = *value;
            changed = true;
        }
    };
    assignIfPresent(next.metadata.handshake_error, update.handshake_error);
    assignIfPresent(next.metadata.status, update.status);
    assignIfPresent(next.metadata.my_pseudo, update.my_pseudo);
    assignIfPresent(next.metadata.other_pseudo, update.other_pseudo);
    assignIfPresent(next.metadata.fake_timezone, update.fake_timezone);
    assignIfPresent(next.metadata.read_messages_count, update.read_messages_count);
    assignIfPresent(next.metadata.is_hidden, update.is_hidden);
    assignIfPresent(next.metadata.anomaly_message, update.anomaly_message);
    assignIfPresent(next.metadata.last_heartbeat, update.last_heartbeat);
    if (update.my_x25519_pub || update.my_x25519_priv || update.announced_x25519_pub || update.private_node_discussion)
    {
        auto vault = std::make_shared<LobbyCryptoVault>();
        const std::string curPub = next.crypto ? next.crypto->my_x25519_pub : "";
        const SecureString curPriv = next.crypto ? next.crypto->my_x25519_priv : SecureString{};
        const std::string curAnn = next.crypto ? next.crypto->announced_x25519_pub : "";
        const SecureString curDisc = next.crypto ? next.crypto->private_node_discussion : SecureString{};
        vault->my_x25519_pub = update.my_x25519_pub.value_or(curPub);
        vault->my_x25519_priv = update.my_x25519_priv.value_or(curPriv);
        vault->announced_x25519_pub = update.announced_x25519_pub.value_or(curAnn);
        vault->private_node_discussion = update.private_node_discussion.value_or(curDisc);
        changed = changed || (!next.crypto || !(*vault == *next.crypto));
        next.crypto = std::move(vault);
    }
    assignIfPresent(next.sync.my_seq_num, update.my_seq_num);
    assignIfPresent(next.sync.announced_seq_num, update.announced_seq_num);
    assignIfPresent(next.sync.dht_acked_seq_nums, update.dht_acked_seq_nums);
    assignIfPresent(next.sync.peer_acked_seq_nums, update.peer_acked_seq_nums);
    assignIfPresent(next.sync.resynced_seq_nums, update.resynced_seq_nums);
    if (update.new_message)
    {
        HistoryRecord record{*update.new_message, std::move(loader)};
        if (record.load)
            record.metadata.content.clear();
        auto &records = m_lobbyData.history.records;
        if (records.size() == records.capacity())
            records.reserve(records.size() + std::max<std::size_t>(records.size(), 1));
        const auto identity = std::pair{record.metadata.direction, record.metadata.seq_num};
        const auto inserted = m_messageIds.emplace(identity);
        try
        {
            if (record.metadata.direction == MessageDirection::OUT)
                m_outgoingIndex.emplace(record.metadata.seq_num, records.size());
        }
        catch (...)
        {
            if (inserted.second)
                m_messageIds.erase(inserted.first);
            throw;
        }
        static_assert(std::is_nothrow_move_constructible_v<HistoryRecord>);
        records.push_back(std::move(record)); // Reserved capacity and noexcept move.
    }
    using std::swap;
    static_assert(std::is_nothrow_swappable_v<LobbyMetadata>);
    static_assert(std::is_nothrow_swappable_v<LobbySyncState>);
    swap(m_lobbyData.metadata, next.metadata);
    swap(m_lobbyData.sync, next.sync);
    swap(m_lobbyData.crypto, next.crypto);
    if (changed)
        m_hasUpdate.store(true, std::memory_order_release);
}

LobbyUpdate Lobby::normalizeUpdateUnlocked(LobbyUpdate update) const
{
    const auto &state = m_lobbyData;
    if (update.new_message && (update.new_message->seq_num <= 0 ||
                               (update.new_message->direction != MessageDirection::IN && update.new_message->direction != MessageDirection::OUT)))
        throw std::invalid_argument("Invalid message identity");
    if (update.my_seq_num && *update.my_seq_num < state.sync.my_seq_num)
        throw std::invalid_argument("Outgoing sequence cannot regress");
    if (update.announced_seq_num && *update.announced_seq_num < 0)
        throw std::invalid_argument("Invalid announced sequence");
    const auto messageSeq = update.new_message && update.new_message->direction == MessageDirection::OUT
                                ? update.new_message->seq_num
                                : state.sync.my_seq_num;
    const auto issued = std::max(state.sync.my_seq_num, messageSeq);
    if (update.my_seq_num && *update.my_seq_num != issued)
        throw std::invalid_argument("Outgoing sequence must correspond to a stored message");
    if (update.message_sent_confirm && (*update.message_sent_confirm <= 0 || *update.message_sent_confirm > issued))
        throw std::invalid_argument("Confirmation exceeds issued sequence");
    auto mergeReceipts = [issued](std::optional<AckState> &requested, const AckState &current)
    {
        if (!requested)
            return;
        if (requested->through > static_cast<std::uint64_t>(issued) || std::ranges::any_of(requested->selective,
                                                                                           [issued](auto seq)
                                                                                           { return seq > static_cast<std::uint64_t>(issued); }))
            throw std::invalid_argument("ACK exceeds issued sequence");
        requested->through = std::max(requested->through, current.through);
        requested->selective.insert(requested->selective.end(), current.selective.begin(), current.selective.end());
        requested->compress();
        if (*requested == current)
            requested.reset();
    };
    mergeReceipts(update.peer_acked_seq_nums, state.sync.peer_acked_seq_nums);
    mergeReceipts(update.dht_acked_seq_nums, state.sync.dht_acked_seq_nums);
    if (update.new_message && m_messageIds.contains({update.new_message->direction, update.new_message->seq_num}))
        update.new_message.reset();
    if (update.message_sent_confirm && state.sync.dht_acked_seq_nums.acknowledges(static_cast<std::uint64_t>(*update.message_sent_confirm)))
        update.message_sent_confirm.reset();
    if (update.read_messages_count)
    {
        if (*update.read_messages_count > state.history.records.size() + (update.new_message ? 1u : 0u))
            throw std::invalid_argument("Read cursor exceeds history size");
        if (*update.read_messages_count <= state.metadata.read_messages_count)
            update.read_messages_count.reset();
    }
    if (update.announced_seq_num && *update.announced_seq_num <= state.sync.announced_seq_num)
        update.announced_seq_num.reset();
    auto omitUnchanged = [](auto &field, const auto &current)
    {
        if (field && *field == current)
            field.reset();
    };
    omitUnchanged(update.handshake_error, state.metadata.handshake_error);
    omitUnchanged(update.status, state.metadata.status);
    omitUnchanged(update.my_pseudo, state.metadata.my_pseudo);
    omitUnchanged(update.other_pseudo, state.metadata.other_pseudo);
    omitUnchanged(update.fake_timezone, state.metadata.fake_timezone);
    omitUnchanged(update.is_hidden, state.metadata.is_hidden);
    omitUnchanged(update.anomaly_message, state.metadata.anomaly_message);
    omitUnchanged(update.last_heartbeat, state.metadata.last_heartbeat);
    omitUnchanged(update.my_seq_num, state.sync.my_seq_num);
    if (state.crypto)
    {
        omitUnchanged(update.my_x25519_pub, state.crypto->my_x25519_pub);
        omitUnchanged(update.my_x25519_priv, state.crypto->my_x25519_priv);
        omitUnchanged(update.announced_x25519_pub, state.crypto->announced_x25519_pub);
        omitUnchanged(update.private_node_discussion, state.crypto->private_node_discussion);
    }
    if (update.resynced_seq_nums)
    {
        update.resynced_seq_nums->insert(update.resynced_seq_nums->end(), state.sync.resynced_seq_nums.begin(), state.sync.resynced_seq_nums.end());
        auto &sequences = *update.resynced_seq_nums;
        std::ranges::sort(sequences);
        sequences.erase(std::unique(sequences.begin(), sequences.end()), sequences.end());
        omitUnchanged(update.resynced_seq_nums, state.sync.resynced_seq_nums);
    }
    return update;
}
