#include "LogEvents.h"
#include "AppFacade.h"

#include <spdlog/spdlog.h>

#include "AnomalyHandler.h"
#include "DhtChatHandler.h"
#include "NetworkResyncHandler.h"

constexpr auto POLLING_TIMEOUT = std::chrono::milliseconds(5000);

AppFacade::AppFacade(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, bool autoPullMessages) : m_dhtPublisher(std::move(dhtPublisher)), m_lobbyStore(std::move(lobbyStore)), m_autoPullMessages(autoPullMessages)
{
    if (!m_dhtPublisher || !m_lobbyStore)
        throw std::invalid_argument("AppFacade requires publisher and store");
    m_chatHandler = std::make_unique<DhtChatHandler>(m_dhtPublisher, m_lobbyStore);
    m_networkResyncer = std::make_shared<NetworkResyncHandler>(m_dhtPublisher, m_lobbyStore);
    m_networkResyncer->attach(m_dhtPublisher->getDhtManager());
    m_syncThread = std::jthread([this](std::stop_token stoken) { syncThreadLoop(stoken); }, m_stopSource.get_token());
}

void AppFacade::shutdown()
{
    m_ownerThread.requireOwner();
    requestStop();
    if (m_syncThread.joinable())
    {
        m_syncThread.join();
    }
    m_networkResyncer->detach();
}

AppFacade::~AppFacade()
{
    shutdown();
}

std::vector<std::shared_ptr<LobbyObserver>> AppFacade::getLobbies() const
{
    return m_lobbyStore->getLobbies();
}

void AppFacade::updateMasterPassword(const std::string &newPassword)
{
    SPDLOG_INFO("[USER] Password rotation requested");
    m_lobbyStore->updateMasterPassword(newPassword);
}

void AppFacade::flagAnomaly(const std::string &lobbyName, const std::string &message)
{
    SPDLOG_INFO("[USER] lobby={} | Anomaly report requested", LogEvents::lobbyId(lobbyName));
    m_lobbyStore->execute(lobbyName, lobby::FlagAnomaly{message});

    AnomalyHandler handler(m_dhtPublisher);
    handler.dispatchAnomalyMessage(lobbyName, message);
}

HandshakeKeyService::ChannelKeys AppFacade::initiateHandshake(const std::string &passphrase, const std::string &pseudo)
{
    if (m_lobbyStore->getLobby(passphrase) != nullptr)
    {
        SPDLOG_WARN("[BOOT] Cannot create lobby {}: Lobby already exists in store.", LogEvents::lobbyId(passphrase));
        throw std::runtime_error("Lobby already exists!");
    }

    SPDLOG_INFO("[USER] lobby={} | Lobby creation requested", LogEvents::lobbyId(passphrase));
    HandshakeKeyService::ChannelKeys channelKeys = m_dhtPublisher->deriveChannelKeys(passphrase);

    m_lobbyStore->execute(passphrase, lobby::BeginHandshake{pseudo});

    SPDLOG_INFO("[HANDSHAKE] lobby={} target_sha1={} public_key_ed25519={} | Rendezvous prepared", LogEvents::lobbyId(passphrase), channelKeys.targetIDHex, channelKeys.pubKeyHex);
    return channelKeys;
}

void AppFacade::synchronizeAllActiveLobbies()
{
    auto lobbies = getLobbies();
    for (auto &lobby : lobbies)
    {
        synchronizeAndProcessNewIncomingMessage(lobby);
    }
}

void AppFacade::synchronizeAndProcessNewIncomingMessage(std::shared_ptr<LobbyObserver> lobbyObs)
{
    auto meta = lobbyObs->getMetadata();
    if (meta.is_hidden || (meta.status != LobbyStatus::HANDSHAKE_DONE && meta.status != LobbyStatus::ANOMALY))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_syncMutex);
    auto it = std::find(m_activeSyncLobbies.begin(), m_activeSyncLobbies.end(), lobbyObs);
    if (it == m_activeSyncLobbies.end())
    {
        m_activeSyncLobbies.push_back(lobbyObs);
        ++m_syncGeneration;
        m_syncCv.notify_one();
    }
}

void AppFacade::syncThreadLoop(std::stop_token stoken)
{

    while (!stoken.stop_requested())
    {
        std::uint64_t generation = 0;
        try
        {
            if (m_autoPullMessages)
            {
                synchronizeAllActiveLobbies();
            }

            std::vector<std::shared_ptr<LobbyObserver>> lobbiesToSync;
            {
                std::lock_guard<std::mutex> lock(m_syncMutex);
                std::erase_if(m_activeSyncLobbies, [](const std::shared_ptr<LobbyObserver> &obs) {
                    if (!obs) return true;
                    auto meta = obs->getMetadata();
                    return meta.is_hidden || (meta.status != LobbyStatus::HANDSHAKE_DONE && meta.status != LobbyStatus::ANOMALY);
                });
                lobbiesToSync = m_activeSyncLobbies;
                generation = m_syncGeneration;
            }

            for (auto &obs : lobbiesToSync)
            {
                m_dhtPublisher->enqueuePollingTask(obs->getLobbyName(), "messages",
                                                   [pub = m_dhtPublisher, obs]
                                                   { pub->fetchLatestSequence(obs); });
            }
            // Durable outgoing messages must retry even when automatic incoming polling is disabled.
            m_networkResyncer->retryAllUnconfirmedMessages();
        }
        catch (const std::exception &e)
        {
            SPDLOG_ERROR("[BOOT] Polling iteration failed: {}", e.what());
        }
        catch (...)
        {
            SPDLOG_ERROR("[BOOT] Polling iteration threw a non-standard exception");
        }
        std::unique_lock<std::mutex> cv_lock(m_syncMutex);
        m_syncCv.wait_for(cv_lock, stoken, POLLING_TIMEOUT, [&]
                          { return generation != m_syncGeneration; });
    }
}

void AppFacade::hideLobby(const std::string &lobbyName)
{
    SPDLOG_INFO("[USER] lobby={} | Hide requested", LogEvents::lobbyId(lobbyName));
    m_lobbyStore->execute(lobbyName, lobby::SetVisibility{true});

    const auto lobby = m_lobbyStore->getLobby(lobbyName);
    std::lock_guard lock(m_syncMutex);
    std::erase(m_activeSyncLobbies, lobby);
    ++m_syncGeneration;
    m_syncCv.notify_one();
}

bool AppFacade::unhideLobbyIfExists(const std::string &lobbyName)
{
    auto lobby = m_lobbyStore->getLobby(lobbyName);
    if (lobby && lobby->getMetadata().is_hidden)
    {
        SPDLOG_INFO("[USER] lobby={} | Restore requested", LogEvents::lobbyId(lobbyName));
        m_lobbyStore->execute(lobbyName, lobby::SetVisibility{false});
        return true;
    }
    return false;
}

DhtChat *AppFacade::chatForLobby(std::shared_ptr<LobbyObserver> lobby)
{
    return m_chatHandler->getChatForLobby(std::move(lobby));
}

void AppFacade::requestStop()
{
    m_stopSource.request_stop();
    m_syncCv.notify_all();
}
