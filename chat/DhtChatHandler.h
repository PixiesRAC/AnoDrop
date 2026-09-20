#pragma once

#include "DhtChat.h"
#include <vector>
#include <memory>

class DhtPublisher;
class LobbyStore;

class DhtChatHandler
{
  public:
    DhtChatHandler();
    ~DhtChatHandler();

    void setDhtPublisher(std::shared_ptr<DhtPublisher> pub);

    void addChat(Lobby &lobby);
    DhtChat* getChat(size_t index);
    size_t getChatCount() const;
    void clearChats();

  private:
    std::vector<std::unique_ptr<DhtChat>> dht_chats;
    std::shared_ptr<DhtPublisher> m_dhtPublisher;
};
