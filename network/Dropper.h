#pragma once

#include "chat/DhtChatUtils.h"
#include "lobby/LobbyStore.h"
#include <array>
#include <libtorrent/session.hpp>
#include <memory>
#include <string>

class DhtManager;

class Dropper
{
    friend class DhtPublisher;

  private:
    Dropper(lt::session *session, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey);

  public:
    ~Dropper();

    bool dropHandShakeMessage(const std::string &message, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore);
    bool dropAnomalyMessage(const std::string &message, const std::string &lobbyName);
    bool dropChatMessage(const ChatPublishParams &params, bool isSeqNumMsg, DhtManager *dhtManager = nullptr);

  private:
    lt::session *m_session;
    std::array<char, 32> m_publicKey;
    std::array<char, 64> m_privateKey;
};