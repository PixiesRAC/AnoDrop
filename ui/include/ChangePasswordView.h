#pragma once

#include "AppFacade.h"
#include "IView.h"

namespace ui
{
class ChangePasswordView : public IView
{
  public:
    ChangePasswordView(std::shared_ptr<AppFacade> appFacade);
    void show() override;

  private:
    std::shared_ptr<AppFacade> m_appFacade;
};
} // namespace ui
