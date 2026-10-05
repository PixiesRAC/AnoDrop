#pragma once

#include "AppFacade.h"

class DhtChatHandler;

namespace ui
{
class GuiBuilder
{
  public:
    GuiBuilder();
    ~GuiBuilder();

    void setAppFacade(std::shared_ptr<AppFacade> appFacade);

    void showLoadingScreen();
    void buildAndRun();

  private:
    void showMainMenu();

    bool m_running;
    int m_selectedOption;
    std::shared_ptr<DhtPublisher> m_dhtPublisher;
    std::shared_ptr<LobbyStore> m_lobbyStore;
    std::shared_ptr<AppFacade> m_appFacade;
    std::unique_ptr<::DhtChatHandler> m_chatHandler;
};
} // namespace ui
