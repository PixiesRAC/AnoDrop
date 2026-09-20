#include "Lobby.h"
#include <iostream>

Lobby::~Lobby() = default;

void Lobby::onUpdate(const nlohmann::json &jsonPatch)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    nlohmann::json cleanPatch = jsonPatch;

    if (cleanPatch.contains("new_message"))
    {
        try
        {
            m_lobbyData.history.messages.push_back(cleanPatch["new_message"].get<ChatMessage>());
            m_hasUpdate = true;
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
        auto pos = std::lower_bound(m_lobbyData.acked_seq_nums.begin(), m_lobbyData.acked_seq_nums.end(), seq);
        if (pos == m_lobbyData.acked_seq_nums.end() || *pos != seq)
        {
            m_lobbyData.acked_seq_nums.insert(pos, seq);
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
            
            // Dispatch a patch to notify LobbyStore to save resynced_seq_nums
            // Since we modified it here, we just need to trigger a save.
            // But actually we don't dispatch patches from here, we just modify our state.
            // Wait, if we don't emit a patch, LobbyStore won't know. 
            // The patch `message_sent_confirm` is processed by LobbyStore first, then it calls onUpdate here.
            // So LobbyStore WILL save it because we update LobbyStore's logic too!
        }
        
        cleanPatch.erase("message_sent_confirm");
    }

    if (cleanPatch.contains("add_pending_resyncs"))
    {
        auto newPending = cleanPatch["add_pending_resyncs"].get<std::map<std::int64_t, std::int64_t>>();
        for (const auto& [k, v] : newPending)
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

    nlohmann::json j = m_lobbyData;
    j.merge_patch(cleanPatch);
    LobbyData newData = j.get<LobbyData>();

    // Notify UI only if data actually changed
    if (m_lobbyData != newData)
    {
        m_lobbyData = newData;
        m_hasUpdate = true;
    }
}
