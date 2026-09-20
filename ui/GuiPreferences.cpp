#include "ui/GuiPreferences.h"
#include <nlohmann/json.hpp>

namespace ui
{

GuiPreferences::GuiPreferences(std::shared_ptr<LobbyStore> lobbyStore)
    : m_lobbyStore(std::move(lobbyStore))
{
}

void GuiPreferences::load(const std::vector<LobbyData> &lobbies, std::vector<int> &openChats)
{
    if (!m_lobbyStore)
        return;

    nlohmann::json prefs = m_lobbyStore->loadPreferences();
    if (prefs.contains("open_chats"))
    {
        openChats.clear();
        for (const auto &savedPubKeyNode : prefs["open_chats"])
        {
            if (!savedPubKeyNode.is_string())
                continue;
            std::string savedPubKey = savedPubKeyNode.get<std::string>();
            // Find which index this pubKey corresponds to
            for (size_t i = 0; i < lobbies.size(); ++i)
            {
                if (lobbies[i].my_x25519_pub == savedPubKey)
                {
                    openChats.push_back(static_cast<int>(i));
                    break;
                }
            }
        }
    }
}

void GuiPreferences::save(const std::vector<LobbyData> &lobbies, const std::vector<int> &openChats)
{
    if (!m_lobbyStore)
        return;

    std::vector<std::string> openPubKeys;
    for (int idx : openChats)
    {
        if (idx >= 0 && idx < static_cast<int>(lobbies.size()))
        {
            openPubKeys.push_back(lobbies[idx].my_x25519_pub);
        }
    }
    nlohmann::json newPrefs;
    newPrefs["open_chats"] = openPubKeys;
    m_lobbyStore->savePreferences(newPrefs);
}

} // namespace ui
