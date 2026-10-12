#pragma once

#include "AppFacade.h"
#include "DhtChatHandler.h"
#include "IView.h"

namespace ui
{
class LobbiesView : public IView
{
  public:
    explicit LobbiesView(std::shared_ptr<AppFacade> appFacade);
    void show() override;

  private:
    std::shared_ptr<AppFacade> m_appFacade;
};
} // namespace ui
