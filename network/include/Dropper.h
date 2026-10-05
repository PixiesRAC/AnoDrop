#pragma once

#include <libtorrent/session.hpp>

#include "DhtChatUtils.h"
#include "LobbyStore.h"

class DhtManager;

class Dropper
{
    friend class DhtPublisher;

  private:
    Dropper(lt::session *session, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey);

  public:
    ~Dropper();

    bool dropHandShakeMessage(const std::string &message, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore, DhtManager *dhtManager = nullptr);
    bool dropAnomalyMessage(const std::string &message, const std::string &lobbyName);
    bool dropHeartbeatMessage(int offsetSeconds, const std::string &lobbyName);
    bool dropAckMessage(const std::string &message, const std::string &lobbyName, int offsetSeconds);
    bool dropChatMessage(const ChatPublishParams &params, bool isSeqNumMsg, DhtManager *dhtManager = nullptr);

  private:
    lt::session *m_session;
    std::array<char, 32> m_publicKey;
    std::array<char, 64> m_privateKey;
};