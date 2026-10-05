#include <nlohmann/json.hpp>

#pragma once

#include "LobbyData.h"

class LobbyObserver
{
  public:
    LobbyObserver() = default;
    virtual ~LobbyObserver() = default;

    virtual void onUpdate(nlohmann::json &jsonPatch) = 0;
    virtual std::string getLobbyName() const = 0;
    virtual LobbyData getLobbyData() const = 0;
    virtual void setFetchingSeq() = 0;
    virtual void clearFetchingSeq() = 0;
    virtual bool isFetchingSeq() const = 0;
    virtual bool consumeUpdateFlag() = 0;
};