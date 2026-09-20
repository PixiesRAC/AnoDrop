#pragma once
#include "core/AppFacade.h"
#include <memory>

#include "ui/IView.h"

namespace ui {
class HandshakeView : public IView {
public:
    HandshakeView(std::shared_ptr<AppFacade> appFacade);
    void show() override;
private:
    std::shared_ptr<AppFacade> m_appFacade;
};
}
