#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "Lobby.h"

#include <iostream>
#include <ranges>
#include <spdlog/spdlog.h>

Lobby::~Lobby() = default;

void Lobby::onUpdate(nlohmann::json &jsonPatch)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    nlohmann::json cleanPatch = jsonPatch;

    if (cleanPatch.contains("new_message"))
    {
        try
        {
            auto msg = cleanPatch["new_message"].get<ChatMessage>();
            auto& messages = m_lobbyData.history.messages;
            
            // Deduplication and Migration check (O(1) for recent messages)
            bool isDuplicate = false;
            
            // Limit deduplication to the last 50 messages to ensure O(1) performance
            auto view = std::views::reverse(messages) | std::views::take(50);
            for (const auto& existing : view)
            {
                if (existing.seq_num == msg.seq_num && existing.direction == msg.direction)
                {
                    isDuplicate = true;
                    break;
                }
            }
            if (!isDuplicate)
            {
                m_hasUpdate.store(true, std::memory_order_release);
            }
            else
            {
                // Remove it from the original patch so LobbyStore doesn't blindly append it to disk
                jsonPatch.erase("new_message");
            }

            if (msg.direction == MessageDirection::IN)
            {
                if (msg.seq_num > 0 && !m_lobbyData.downloaded_seq_nums.acknowledges(static_cast<std::uint64_t>(msg.seq_num)))
                {
                    m_lobbyData.downloaded_seq_nums.selective.push_back(static_cast<std::uint64_t>(msg.seq_num));
                }
                
                std::string resyncPrefix = "[Resync of Seq:";
                if (msg.content.find(resyncPrefix) == 0)
                {
                    try {
                        std::string seqStr = msg.content.substr(resyncPrefix.length());
                        std::int64_t seq = std::stoll(seqStr);
                        if (seq > 0 && !m_lobbyData.downloaded_seq_nums.acknowledges(static_cast<std::uint64_t>(seq)))
                        {
                            m_lobbyData.downloaded_seq_nums.selective.push_back(static_cast<std::uint64_t>(seq));
                        }
                    } catch(...) {}
                }

                // Compress the AckState window
                m_lobbyData.downloaded_seq_nums.compress();
            }
            else if (msg.direction == MessageDirection::OUT)
            {
                if (msg.seq_num > m_lobbyData.my_seq_num)
                {
                    m_lobbyData.my_seq_num = msg.seq_num;
                }
            }

            if (!isDuplicate)
            {
                messages.push_back(std::move(msg));
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error parsing new_message: " << e.what() << std::endl;
        }
        cleanPatch.erase("new_message");
    }

    if (cleanPatch.contains("message_sent_confirm"))
    {
        std::int64_t seq = cleanPatch["message_sent_confirm"].get<std::int64_t>();
        if (!m_lobbyData.dht_acked_seq_nums.acknowledges(static_cast<std::uint64_t>(seq)))
        {
            m_lobbyData.dht_acked_seq_nums.selective.push_back(static_cast<std::uint64_t>(seq));
            m_lobbyData.dht_acked_seq_nums.compress();

            m_hasUpdate = true;
        }

        if (m_pendingResyncs.count(seq))
        {
            std::int64_t old_seq = m_pendingResyncs[seq];
            auto rpos = std::lower_bound(m_lobbyData.resynced_seq_nums.begin(), m_lobbyData.resynced_seq_nums.end(), old_seq);
            if (rpos == m_lobbyData.resynced_seq_nums.end() || *rpos != old_seq)
            {
                m_lobbyData.resynced_seq_nums.insert(rpos, old_seq);
            }
            m_pendingResyncs.erase(seq);
            m_hasUpdate = true;
        }

        cleanPatch.erase("message_sent_confirm");
    }

    if (cleanPatch.contains("add_pending_resyncs"))
    {
        auto newPending = cleanPatch["add_pending_resyncs"].get<std::map<std::int64_t, std::int64_t>>();
        for (const auto &[k, v] : newPending)
        {
            m_pendingResyncs[k] = v;
        }
        cleanPatch.erase("add_pending_resyncs");
    }

    if (cleanPatch.contains("message_resynced"))
    {
        std::vector<std::int64_t> seqs = cleanPatch["message_resynced"].get<std::vector<std::int64_t>>();
        for (std::int64_t seq : seqs)
        {
            auto pos = std::lower_bound(m_lobbyData.resynced_seq_nums.begin(), m_lobbyData.resynced_seq_nums.end(), seq);
            if (pos == m_lobbyData.resynced_seq_nums.end() || *pos != seq)
            {
                m_lobbyData.resynced_seq_nums.insert(pos, seq);
                m_hasUpdate = true;
            }
        }
        cleanPatch.erase("message_resynced");
    }

    if (cleanPatch.empty())
        return;

    bool changed = false;
    for (auto it = cleanPatch.begin(); it != cleanPatch.end(); ++it)
    {
        const auto &key = it.key();
        const auto &val = it.value();
        try
        {
            if (key == "status") {
                auto newStatus = val.is_number() ? static_cast<LobbyStatus>(val.get<int>()) : val.get<LobbyStatus>();
                if (m_lobbyData.status != newStatus) { m_lobbyData.status = newStatus; changed = true; }
            }
            else if (key == "my_pseudo") {
                auto s = val.get<std::string>();
                if (m_lobbyData.my_pseudo != s) { m_lobbyData.my_pseudo = std::move(s); changed = true; }
            }
            else if (key == "other_pseudo") {
                auto s = val.get<std::string>();
                if (m_lobbyData.other_pseudo != s) { m_lobbyData.other_pseudo = std::move(s); changed = true; }
            }
            else if (key == "my_x25519_pub") {
                auto s = val.get<std::string>();
                if (m_lobbyData.my_x25519_pub != s) { m_lobbyData.my_x25519_pub = std::move(s); changed = true; }
            }
            else if (key == "my_x25519_priv") {
                auto s = val.get<std::string>();
                if (m_lobbyData.my_x25519_priv != s) { m_lobbyData.my_x25519_priv = std::move(s); changed = true; }
            }
            else if (key == "announced_x25519_pub") {
                auto s = val.get<std::string>();
                if (m_lobbyData.announced_x25519_pub != s) { m_lobbyData.announced_x25519_pub = std::move(s); changed = true; }
            }
            else if (key == "private_node_discussion") {
                auto s = val.get<std::string>();
                if (m_lobbyData.private_node_discussion != s) { m_lobbyData.private_node_discussion = std::move(s); changed = true; }
            }
            else if (key == "my_seq_num") {
                auto n = val.get<std::int64_t>();
                if (m_lobbyData.my_seq_num != n) { m_lobbyData.my_seq_num = n; changed = true; }
            }
            else if (key == "announced_seq_num") {
                auto n = val.get<std::int64_t>();
                if (m_lobbyData.announced_seq_num != n) { m_lobbyData.announced_seq_num = n; changed = true; }
            }
            else if (key == "dht_acked_seq_nums") {
                auto a = val.get<AckState>();
                if (m_lobbyData.dht_acked_seq_nums != a) { m_lobbyData.dht_acked_seq_nums = std::move(a); changed = true; }
            }
            else if (key == "peer_acked_seq_nums") {
                auto a = val.get<AckState>();
                if (m_lobbyData.peer_acked_seq_nums != a) { m_lobbyData.peer_acked_seq_nums = std::move(a); changed = true; }
            }
            else if (key == "downloaded_seq_nums") {
                auto a = val.get<AckState>();
                if (m_lobbyData.downloaded_seq_nums != a) { m_lobbyData.downloaded_seq_nums = std::move(a); changed = true; }
            }
            else if (key == "resynced_seq_nums") {
                auto r = val.get<std::vector<std::int64_t>>();
                if (m_lobbyData.resynced_seq_nums != r) { m_lobbyData.resynced_seq_nums = std::move(r); changed = true; }
            }
            else if (key == "fake_timezone") {
                auto tz = val.get<FakeTimezone>();
                if (m_lobbyData.fake_timezone != tz) { m_lobbyData.fake_timezone = std::move(tz); changed = true; }
            }
            else if (key == "read_messages_count") {
                auto c = val.get<std::size_t>();
                if (m_lobbyData.read_messages_count != c) { m_lobbyData.read_messages_count = c; changed = true; }
            }
            else if (key == "is_hidden") {
                auto h = val.get<bool>();
                if (m_lobbyData.is_hidden != h) { m_lobbyData.is_hidden = h; changed = true; }
            }
            else if (key == "anomaly_message") {
                auto s = val.get<std::string>();
                if (m_lobbyData.anomaly_message != s) { m_lobbyData.anomaly_message = std::move(s); changed = true; }
            }
            else if (key == "last_heartbeat") {
                auto t = val.get<int64_t>();
                if (m_lobbyData.last_heartbeat != t) { m_lobbyData.last_heartbeat = t; changed = true; }
            }
        }
        catch (const std::exception &e)
        {
            SPDLOG_WARN("Error patching field '{}': {}", key, e.what());
        }
    }

    if (changed)
    {
        m_hasUpdate.store(true, std::memory_order_release);
    }
}
