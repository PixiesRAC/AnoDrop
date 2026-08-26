#pragma once

#include <array>
#include "lobby/LobbyStore.h"
#include <libtorrent/session.hpp>
#include <memory>
#include <string>

class Dropper
{
  public:
    Dropper(lt::session *session, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey);
    ~Dropper();

    bool dropMessage(const std::string &message, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore);

  private:
    lt::session *m_session;
    std::array<char, 32> m_publicKey;
    std::array<char, 64> m_privateKey;
};