#pragma once

#include "core/DhtPublisher.h"
#include "lobby/LobbyStore.h"
#include <memory>
#include <vector>
#include <string>
#include "core/HandshakeManager.h"

class AppFacade
{
  public:
    AppFacade(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore);
    ~AppFacade();

    std::vector<std::shared_ptr<LobbyObserver>> getLobbies() const;
    void updateMasterPassword(const std::string& newPassword);
    HandshakeManager::ChannelKeys initiateHandshake(const std::string& passphrase, const std::string& pseudo);

  private:
    std::shared_ptr<DhtPublisher> m_dhtPublisher;
    std::shared_ptr<LobbyStore> m_lobbyStore;
};