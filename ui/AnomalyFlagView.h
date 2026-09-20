#pragma once
#include "core/AppFacade.h"
#include "lobby/LobbyStore.h"
#include "ui/IView.h"
#include <memory>

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
