#pragma once

#include "DhtChat.h"

namespace chat
{

class ChatResyncer
{
  public:
    static int resyncPendingMessages(LobbyObserver &lobbyObserver, DhtChat &dhtChat);
};

} // namespace chat
