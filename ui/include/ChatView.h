#pragma once

#include "DhtChat.h"
#include "IView.h"

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

    std::string formatMessagePrefix(const ChatMessage &msg, const LobbyData &lobbyData) const;
};
} // namespace ui
