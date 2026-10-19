#pragma once
#include "LobbyObserver.h"

class LobbyTestAccess
{
  public:
    static void onUpdate(LobbyObserver &lobby, LobbyUpdate &update, MessageLoader loader = {})
    {
        lobby.onUpdate(update, std::move(loader));
    }
    static void resetHistoryRecords(LobbyObserver &lobby, std::vector<HistoryRecord> records)
    {
        lobby.resetHistoryRecords(std::move(records));
    }
    static LobbyUpdate normalizeUpdate(const LobbyObserver &lobby, LobbyUpdate update)
    {
        return lobby.normalizeUpdate(std::move(update));
    }
};
