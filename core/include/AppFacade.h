#pragma once

#include "DhtPublisher.h"

class NetworkResyncHandler;
class DhtChatHandler;

class AppFacade
{
  public:
    AppFacade(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, bool autoPullMessages = false);
    ~AppFacade();

    void shutdown();

    [[nodiscard]] std::vector<std::shared_ptr<LobbyObserver>> getLobbies() const;
    void updateMasterPassword(const std::string &newPassword);
    [[nodiscard]] HandshakeKeyService::ChannelKeys initiateHandshake(const std::string &passphrase, const std::string &pseudo);
    void synchronizeAllActiveLobbies();
    void synchronizeAndProcessNewIncomingMessage(std::shared_ptr<LobbyObserver> lobbyObs);
    void hideLobby(const std::string &lobbyName);
    bool unhideLobbyIfExists(const std::string &lobbyName);
    void flagAnomaly(const std::string &lobbyName, const std::string &message);

    [[nodiscard]] DhtChatHandler *getChatHandler() const { return m_chatHandler.get(); }

  private:
    void syncThreadLoop(std::stop_token stoken);

    std::shared_ptr<DhtPublisher> m_dhtPublisher;
    std::shared_ptr<LobbyStore> m_lobbyStore;
    std::unique_ptr<NetworkResyncHandler> m_networkResyncer;
    std::unique_ptr<DhtChatHandler> m_chatHandler;

    std::jthread m_syncThread;
    mutable std::mutex m_syncMutex;
    std::condition_variable_any m_syncCv;
    std::vector<std::shared_ptr<LobbyObserver>> m_activeSyncLobbies;
    bool m_autoPullMessages;
};