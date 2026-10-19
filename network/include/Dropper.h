#pragma once

#include <libtorrent/session.hpp>

#include "DhtChatUtils.h"
#include "DhtManager.h"
#include "HandshakeKeyService.h"
#include "SigningKey.h"

class Dropper
{
  public:
    Dropper(lt::session *session, const HandshakeKeyService::ChannelKeys &keys);
    ~Dropper();

    bool dropHandShakeMessage(const std::string &message, const std::string &lobbyName, std::int64_t sequence, DhtManager *dhtManager, DhtManager::PutCompletion completion);
    bool dropAnomalyMessage(const std::string &message, const std::string &lobbyName);
    bool dropHeartbeatMessage(int offsetSeconds, const std::string &lobbyName);
    bool dropAckMessage(const std::string &message, const std::string &lobbyName, int offsetSeconds);
    bool dropChatMessage(const ChatPublishParams &params, DhtManager *dhtManager = nullptr, DhtManager::PutCompletion completion = {});
    bool dropSequenceAnnouncement(const ChatPublishParams &params);

  private:
    lt::session *m_session;
    std::array<char, 32> m_publicKey;
    std::shared_ptr<SigningKey> m_privateKey;
    std::string m_salt;
    std::string m_pubKeyHex;
    std::string m_targetIDHex;
};