#pragma once
#include "core/AppFacade.h"
#include <memory>

#include "ui/IView.h"

namespace ui {
class ChangePasswordView : public IView {
public:
    ChangePasswordView(std::shared_ptr<AppFacade> appFacade);
    void show() override;
private:
    std::shared_ptr<AppFacade> m_appFacade;
};
}
