#include "core/AppFacade.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

AppFacade::AppFacade(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore) : m_dhtPublisher(std::move(dhtPublisher)), m_lobbyStore(std::move(lobbyStore))
{
}

AppFacade::~AppFacade()
{
}

std::vector<std::shared_ptr<LobbyObserver>> AppFacade::getLobbies() const
{
    return m_lobbyStore ? m_lobbyStore->getLobbies() : std::vector<std::shared_ptr<LobbyObserver>>{};
}

void AppFacade::updateMasterPassword(const std::string& newPassword)
{
    if (m_lobbyStore)
    {
        m_lobbyStore->updateMasterPassword(newPassword);
    }
}

HandshakeManager::ChannelKeys AppFacade::initiateHandshake(const std::string& passphrase, const std::string& pseudo)
{
    if (!m_dhtPublisher || !m_lobbyStore)
    {
        throw std::runtime_error("Dependencies missing in AppFacade");
    }

    if (m_lobbyStore->getLobby(passphrase) != nullptr)
    {
        SPDLOG_WARN("Cannot create lobby '{}': Lobby already exists in store.", passphrase);
        throw std::runtime_error("Lobby already exists!");
    }

    HandshakeManager::ChannelKeys channelKeys = m_dhtPublisher->deriveChannelKeys(passphrase);

    nlohmann::json patch;
    patch["status"] = LobbyStatus::PENDING_HANDSHAKE;
    patch["my_pseudo"] = pseudo;
    m_lobbyStore->updateLobbyData(passphrase, patch);

    return channelKeys;
}
