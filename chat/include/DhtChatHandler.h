#pragma once
#include <unordered_map>

#include "DhtChat.h"
class DhtChatHandler
{
  public:
    DhtChatHandler(std::shared_ptr<OutgoingMessageQueue> publisher, std::shared_ptr<LobbyStore> store)
        : m_publisher(std::move(publisher)), m_store(std::move(store))
    {
    }
    DhtChat *getChatForLobby(std::shared_ptr<LobbyObserver> lobby);

  private:
    std::shared_ptr<OutgoingMessageQueue> m_publisher;
    std::shared_ptr<LobbyStore> m_store;
    std::unordered_map<std::string, std::unique_ptr<DhtChat>> m_chats;
};
