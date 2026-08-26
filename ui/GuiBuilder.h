#pragma once

#include "core/AppFacade.h"
#include "core/DhtPublisher.h"
#include "lobby/LobbyStore.h"
#include "ui/GuiPreferences.h"

#include <memory>
#include <string>

namespace ui
{

class GuiBuilder
{
  public:
    GuiBuilder();
    ~GuiBuilder();

    void setDhtPublisher(std::shared_ptr<DhtPublisher> rendezvousService);
    void setLobbyStore(std::shared_ptr<LobbyStore> lobbyStore);
    void setAppFacade(std::shared_ptr<AppFacade> appFacade);

    void showLoadingScreen();
    void buildAndRun();

  private:
    void showMainMenu();
    void handleHandShake();
    void handleChangeMasterPassword();
    void revealLobbies();
    void lobbyMenuLoop();

    bool m_running;
    int m_selectedOption;
    std::shared_ptr<DhtPublisher> m_dhtPublisher;
    std::shared_ptr<LobbyStore> m_lobbyStore;
    std::unique_ptr<GuiPreferences> m_preferences;
    std::shared_ptr<AppFacade> m_appFacade;

    std::vector<int> m_openChats;
    bool m_focusRightPane;
    int m_activeChatIndex; // Index in m_openChats
};

} // namespace ui
