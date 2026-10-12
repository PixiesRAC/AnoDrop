#pragma once

#include <string>

namespace ui
{

class Unlock
{
  public:
    [[nodiscard]] static std::string askMasterPassword(const std::string& errorMessage = "");
};

} // namespace ui
