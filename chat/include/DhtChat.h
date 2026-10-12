#pragma once

#include <nlohmann/json.hpp>
#include "Lobby.h"

class DhtPublisher;

class DhtChat
{
  public:
    static constexpr std::int64_t RATE_LIMIT_WINDOW_SECONDS = 10;
    static constexpr int RATE_LIMIT_MAX_MESSAGES = 10;

    DhtChat(Lobby &lobby, std::shared_ptr<DhtPublisher> pub);
    ~DhtChat();

    [[nodiscard]] std::int64_t sendMessage(const std::string &message);
    void republishUnackedMessages();
    void dispatchPatch(const nlohmann::json &patch);
    [[nodiscard]] std::string getLobbyName() const;

  private:
    Lobby &m_lobby;
    std::shared_ptr<DhtPublisher> m_publisher;
};