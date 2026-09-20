#pragma once

#include "lobby/LobbyObserver.h"
#include "chat/DhtChat.h"

namespace chat {

class ChatResyncer {
public:
    static void resyncPendingMessages(LobbyObserver& lobbyObserver, DhtChat& dhtChat);
    static void resyncAllMessages(LobbyObserver& lobbyObserver, DhtChat& dhtChat);
};

} // namespace chat
