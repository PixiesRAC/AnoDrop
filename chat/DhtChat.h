#pragma once

#include "lobby/Lobby.h"
#include <memory>
#include <string>

class DhtPublisher;

class DhtChat
{
  public:
    DhtChat(Lobby &lobby, std::shared_ptr<DhtPublisher> pub);
    ~DhtChat();

    std::int64_t sendMessage(const std::string &message);
    void dispatchPatch(const nlohmann::json& patch);

  private:
    Lobby &m_lobby;
    std::shared_ptr<DhtPublisher> m_publisher;
};