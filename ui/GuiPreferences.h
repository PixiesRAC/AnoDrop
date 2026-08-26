#pragma once

#include "lobby/LobbyStore.h"
#include <memory>
#include <vector>
#include <string>

namespace ui
{

class GuiPreferences
{
  public:
    GuiPreferences(std::shared_ptr<LobbyStore> lobbyStore);

    void load(const std::vector<LobbyData> &lobbies, std::vector<int> &openChats);
    void save(const std::vector<LobbyData> &lobbies, const std::vector<int> &openChats);

  private:
    std::shared_ptr<LobbyStore> m_lobbyStore;
};

} // namespace ui
