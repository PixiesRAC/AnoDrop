#include "Lobby.h"
#include <iostream>

Lobby::~Lobby() = default;

void Lobby::onUpdate(const nlohmann::json &jsonPatch)
{
    nlohmann::json cleanPatch = jsonPatch;

    // Removed the empty string checks, as logic is handled correctly at the source

    if (cleanPatch.empty())
        return;

    nlohmann::json j = m_lobbyData;
    j.merge_patch(cleanPatch);
    LobbyData newData = j.get<LobbyData>();

    // On ne notifie l'UI que si la donnée a réellement changé
    if (nlohmann::json(m_lobbyData) != nlohmann::json(newData))
    {
        m_lobbyData = newData;
        m_hasUpdate = true;
        std::cout << "Lobby " << m_lobbyData.lobby_name << " updated." << std::endl;
    }
}
