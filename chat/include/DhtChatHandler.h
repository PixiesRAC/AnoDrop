#pragma once

#include "DhtChat.h"

class DhtPublisher;
class LobbyStore;

class DhtChatHandler
{
  public:
    DhtChatHandler();
    ~DhtChatHandler();

    void setDhtPublisher(std::shared_ptr<DhtPublisher> pub);

    void addChat(Lobby &lobby);
    [[nodiscard]] DhtChat *getChat(size_t index);
    [[nodiscard]] DhtChat *getChatForLobby(Lobby &lobby);
    [[nodiscard]] size_t getChatCount() const;
    void clearChats();

  private:
    std::vector<std::unique_ptr<DhtChat>> dht_chats;
    std::shared_ptr<DhtPublisher> m_dhtPublisher;
};
