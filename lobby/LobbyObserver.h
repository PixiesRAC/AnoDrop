
#pragma once

#include "LobbyData.h"
#include <nlohmann/json.hpp>
#include <string>

class LobbyObserver
{
  public:
    LobbyObserver() = default;
    virtual ~LobbyObserver() = default;

    virtual void onUpdate(const nlohmann::json &jsonPatch) = 0;
    virtual std::string getLobbyName() const = 0;
    virtual LobbyData getLobbyData() const = 0;
    virtual bool consumeUpdateFlag() = 0;
};