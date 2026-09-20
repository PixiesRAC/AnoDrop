#pragma once
#include "chat/DhtChatHandler.h"
#include "core/AppFacade.h"
#include "ui/GuiPreferences.h"
#include <memory>

#include "ui/IView.h"

namespace ui
{
class LobbiesView : public IView
{
  public:
    LobbiesView(std::shared_ptr<AppFacade> appFacade, GuiPreferences *prefs, DhtChatHandler *chatHandler);
    void show() override;

  private:
    std::shared_ptr<AppFacade> m_appFacade;
    GuiPreferences *m_preferences;
    DhtChatHandler *m_chatHandler;
};
} // namespace ui
