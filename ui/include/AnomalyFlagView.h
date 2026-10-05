#pragma once

#include "AppFacade.h"
#include "IView.h"

namespace ui
{
class AnomalyFlagView : public IView
{
  public:
    AnomalyFlagView(std::shared_ptr<AppFacade> appFacade, std::shared_ptr<LobbyStore> lobbyStore);
    void show() override;

  private:
    std::shared_ptr<AppFacade> m_appFacade;
    std::shared_ptr<LobbyStore> m_lobbyStore;
};
} // namespace ui
