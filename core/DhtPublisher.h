#pragma once

#include "core/HandshakeManager.h"
#include "lobby/LobbyStore.h"
#include "network/DhtManager.h"

#include <memory>
#include <string>

class DhtPublisher
{
  public:
    DhtPublisher() = default;
    ~DhtPublisher() = default;

    void setDhtManager(std::unique_ptr<DhtManager> dhtManager);
    void setLobbyStore(std::shared_ptr<LobbyStore> lobbyStore);

    HandshakeManager::ChannelKeys deriveChannelKeys(const std::string &passphrase);

    void dispatchHandshakeMessage(const std::string &lobbyName, const std::string &pseudo);
    void waitForPutConfirmed(const std::string &context = "");

  private:
    std::unique_ptr<DhtManager> m_dhtManager;
    std::shared_ptr<LobbyStore> m_lobbyStore;

};
