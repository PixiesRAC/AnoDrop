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
    void shutdown();
    static void resetTerminal();

  private:
    void showMainMenu();

    bool m_running;
    int m_selectedOption;
    std::shared_ptr<AppFacade> m_appFacade;
};
} // namespace ui
