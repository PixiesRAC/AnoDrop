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
                messages.push_back(std::move(msg));
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

    auto saved_history = m_lobbyData.history; // Preserve history shared_ptr
    nlohmann::json j = m_lobbyData;
    j.erase("history"); // Prevent deep copy serialization of history

    nlohmann::json nestedPatch = nlohmann::json::object();
    for (auto it = cleanPatch.begin(); it != cleanPatch.end(); ++it) {
        if (it.key() == "my_x25519_pub" || it.key() == "my_x25519_priv" || 
            it.key() == "announced_x25519_pub" || it.key() == "private_node_discussion") {
            nestedPatch["keys"][it.key()] = it.value();
        } else if (it.key() == "my_seq_num" || it.key() == "dht_acked_seq_nums" || it.key() == "peer_acked_seq_nums") {
            nestedPatch["outbound"][it.key()] = it.value();
        } else if (it.key() == "announced_seq_num" || it.key() == "downloaded_seq_nums" || it.key() == "resynced_seq_nums") {
            nestedPatch["inbound"][it.key()] = it.value();
        } else {
            nestedPatch[it.key()] = it.value();
        }
    }

    j.merge_patch(nestedPatch);
    LobbyData newData = j.get<LobbyData>();
    newData.history = saved_history; // Restore history shared_ptr

    // Notify UI only if data actually changed
    if (m_lobbyData != newData)
    {
        m_lobbyData = newData;
        m_hasUpdate = true;
    }
}
