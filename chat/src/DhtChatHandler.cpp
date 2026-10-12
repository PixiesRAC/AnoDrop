#include "DhtChatHandler.h"

DhtChatHandler::DhtChatHandler()
{
}
DhtChatHandler::~DhtChatHandler()
{
}

void DhtChatHandler::setDhtPublisher(std::shared_ptr<DhtPublisher> pub)
{
    m_dhtPublisher = pub;
}

void DhtChatHandler::addChat(Lobby &lobby)
{
    dht_chats.push_back(std::make_unique<DhtChat>(lobby, m_dhtPublisher));
}

DhtChat *DhtChatHandler::getChat(size_t index)
{
    if (index < dht_chats.size())
    {
        return dht_chats[index].get();
    }
    return nullptr;
}

DhtChat *DhtChatHandler::getChatForLobby(Lobby &lobby)
{
    for (auto &chat : dht_chats)
    {
        if (chat && chat->getLobbyName() == lobby.getLobbyName())
        {
            return chat.get();
        }
    }
    dht_chats.push_back(std::make_unique<DhtChat>(lobby, m_dhtPublisher));
    return dht_chats.back().get();
}


size_t DhtChatHandler::getChatCount() const
{
    return dht_chats.size();
}

void DhtChatHandler::clearChats()
{
    dht_chats.clear();
}
