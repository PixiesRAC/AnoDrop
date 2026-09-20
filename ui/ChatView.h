#pragma once
#include "chat/DhtChat.h"
#include "lobby/LobbyObserver.h"
#include <memory>
#include <string>

#include "ui/IView.h"

namespace ui
{
class ChatView : public IView
{
  public:
    ChatView(std::shared_ptr<LobbyObserver> lobbyObserver, DhtChat *dhtChat);
    void show() override;

  private:
    std::shared_ptr<LobbyObserver> m_lobbyObserver;
    DhtChat *m_dhtChat;
    std::string m_currentMessage;
    int m_focusIndex;   // 0 for Input, 1 for Leave Button
    int m_scrollOffset; // Scroll offset for chat history
};
} // namespace ui
