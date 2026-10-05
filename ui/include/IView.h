#pragma once

namespace ui
{
class IView
{
  public:
    virtual ~IView() = default;
    virtual void show() = 0;
};
} // namespace ui
