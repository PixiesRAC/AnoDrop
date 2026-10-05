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

size_t DhtChatHandler::getChatCount() const
{
    return dht_chats.size();
}

void DhtChatHandler::clearChats()
{
    dht_chats.clear();
}
