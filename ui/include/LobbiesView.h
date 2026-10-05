#pragma once

#include "AppFacade.h"
#include "DhtChatHandler.h"
#include "IView.h"

namespace ui
{
class LobbiesView : public IView
{
  public:
    LobbiesView(std::shared_ptr<AppFacade> appFacade, DhtChatHandler *chatHandler);
    void show() override;

  private:
    std::shared_ptr<AppFacade> m_appFacade;
    DhtChatHandler *m_chatHandler;
};
} // namespace ui
