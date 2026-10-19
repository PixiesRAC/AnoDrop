#include "DhtChatHandler.h"
DhtChat* DhtChatHandler::getChatForLobby(std::shared_ptr<LobbyObserver> lobby) {
    if (!lobby) {
        return nullptr;
    }
    auto& chat = m_chats[lobby->getLobbyName()];
    if (!chat) chat = std::make_unique<DhtChat>(std::move(lobby), m_publisher, m_store);
    return chat.get();
}
