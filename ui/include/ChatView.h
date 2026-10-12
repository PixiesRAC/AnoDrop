#pragma once

#include <memory>
#include "ChatViewModel.h"
#include "DhtChat.h"
#include "IView.h"
#include "LobbyObserver.h"

namespace ui
{
class ChatView : public IView
{
  public:
    ChatView(std::shared_ptr<LobbyObserver> lobbyObserver, DhtChat *dhtChat);
    ~ChatView() override = default;

    void show() override;

  private:
    void render(int chatAreaHeight, int chatAreaTop);

    std::unique_ptr<ChatViewModel> m_viewModel;
};
} // namespace ui
