#pragma once

#include "core/DhtPublisher.h"
#include "crypto/HandshakeKeyService.h"
#include "lobby/LobbyStore.h"
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>

class AppFacade
{
  public:
    AppFacade(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore);
    ~AppFacade();

    std::vector<std::shared_ptr<LobbyObserver>> getLobbies() const;
    void updateMasterPassword(const std::string &newPassword);
    HandshakeKeyService::ChannelKeys initiateHandshake(const std::string &passphrase, const std::string &pseudo);
    void synchronizeAndProcessNewIncomingMessage(std::shared_ptr<LobbyObserver> lobbyObs);
    void hideLobby(const std::string &lobbyName);
    bool unhideLobbyIfExists(const std::string &lobbyName);
    void sendMessage(const std::string &lobbyName, const std::string &content);
    void flagAnomaly(const std::string &lobbyName, const std::string &message);

  private:
    void syncThreadLoop(std::stop_token stoken);

    std::shared_ptr<DhtPublisher> m_dhtPublisher;
    std::shared_ptr<LobbyStore> m_lobbyStore;

    std::jthread m_syncThread;
    std::mutex m_syncMutex;
    std::vector<std::shared_ptr<LobbyObserver>> m_activeSyncLobbies;
};