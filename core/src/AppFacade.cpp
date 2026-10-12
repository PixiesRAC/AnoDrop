#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "AppFacade.h"

#include <spdlog/spdlog.h>

#include "AnomalyHandler.h"
#include "NetworkResyncHandler.h"
#include "DhtChatHandler.h"

constexpr auto POLLING_TIMEOUT = std::chrono::milliseconds(5000);
constexpr auto MAX_TIMEOUT_COUNT = 3;

AppFacade::AppFacade(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, bool autoPullMessages) : m_dhtPublisher(std::move(dhtPublisher)), m_lobbyStore(std::move(lobbyStore)), m_autoPullMessages(autoPullMessages)
{
    m_chatHandler = std::make_unique<DhtChatHandler>();
    if (m_dhtPublisher)
    {
        m_chatHandler->setDhtPublisher(m_dhtPublisher);
    }
    m_networkResyncer = std::make_unique<NetworkResyncHandler>(m_dhtPublisher, m_lobbyStore);
    if (m_dhtPublisher && m_dhtPublisher->getDhtManager())
    {
        m_networkResyncer->attach(m_dhtPublisher->getDhtManager());
    }
    m_syncThread = std::jthread(&AppFacade::syncThreadLoop, this);
}

void AppFacade::shutdown()
{
    m_syncThread.request_stop();
    m_syncCv.notify_all();
    if (m_syncThread.joinable())
    {
        m_syncThread.join();
    }
    if (m_networkResyncer)
    {
        m_networkResyncer->detach();
    }
}

AppFacade::~AppFacade()
{
    shutdown();
}

std::vector<std::shared_ptr<LobbyObserver>> AppFacade::getLobbies() const
{
    return m_lobbyStore ? m_lobbyStore->getLobbies() : std::vector<std::shared_ptr<LobbyObserver>>{};
}

void AppFacade::updateMasterPassword(const std::string &newPassword)
{
    if (m_lobbyStore)
    {
        m_lobbyStore->updateMasterPassword(newPassword);
    }
}

void AppFacade::flagAnomaly(const std::string &lobbyName, const std::string &message)
{
    nlohmann::json patch;
    patch["status"] = LobbyStatus::ANOMALY;
    patch["anomaly_message"] = message;
    m_lobbyStore->asyncUpdateLobbyData(lobbyName, patch);

    AnomalyHandler handler(m_dhtPublisher);
    handler.dispatchAnomalyMessage(lobbyName, message);
}

HandshakeKeyService::ChannelKeys AppFacade::initiateHandshake(const std::string &passphrase, const std::string &pseudo)
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

    HandshakeKeyService::ChannelKeys channelKeys = m_dhtPublisher->deriveChannelKeys(passphrase);

    nlohmann::json patch;
    patch["status"] = LobbyStatus::PENDING_HANDSHAKE;
    patch["my_pseudo"] = pseudo;
    m_lobbyStore->asyncUpdateLobbyData(passphrase, patch);

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
    if (!m_dhtPublisher || !m_lobbyStore)
    {
        throw std::runtime_error("Dependencies missing in AppFacade");
    }

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
        m_syncCv.notify_one();
    }
}

void AppFacade::syncThreadLoop(std::stop_token stoken)
{
    std::unordered_map<std::string, int> timeoutCounters;

    while (!stoken.stop_requested())
    {
        if (m_autoPullMessages)
        {
            synchronizeAllActiveLobbies();
        }

        std::vector<std::shared_ptr<LobbyObserver>> lobbiesToSync;
        {
            std::lock_guard<std::mutex> lock(m_syncMutex);
            lobbiesToSync = m_activeSyncLobbies;
        }

        for (auto &obs : lobbiesToSync)
        {
            if (!obs->isFetchingSeq())
            {
                timeoutCounters[obs->getLobbyName()] = 0;
                std::string targetKey;
                obs->withDataView([&targetKey](const LobbyMetadata &, const LobbySyncState &, const LobbyCryptoVault &crypto) {
                    targetKey = crypto.private_node_discussion + crypto.announced_x25519_pub;
                });
                if (m_dhtPublisher)
                {
                    obs->setFetchingSeq();
                    m_dhtPublisher->enqueueNetworkTask(obs->getLobbyName(), [pub = m_dhtPublisher, targetKey, obs]()
                                                       {
                                                           if (pub)
                                                               pub->fetchDhtItem(targetKey, obs, true);
                                                       });
                }
            }
            else
            {
                timeoutCounters[obs->getLobbyName()]++;
                if (timeoutCounters[obs->getLobbyName()] >= MAX_TIMEOUT_COUNT)
                {
                    SPDLOG_WARN("DHT Fetch timeout for lobby {}! Unlocking to retry...", obs->getLobbyName());
                    obs->clearFetchingSeq();
                    timeoutCounters[obs->getLobbyName()] = 0;
                }
            }

            if (m_networkResyncer)
            {
                m_networkResyncer->retryUnconfirmedMessages(obs);
            }
        }
        std::unique_lock<std::mutex> cv_lock(m_syncMutex);
        m_syncCv.wait_for(cv_lock, stoken, POLLING_TIMEOUT, []{ return false; });
    }
}

void AppFacade::hideLobby(const std::string &lobbyName)
{
    if (m_lobbyStore)
    {
        nlohmann::json patch = {{"is_hidden", true}};
        m_lobbyStore->asyncUpdateLobbyData(lobbyName, patch);
    }

    std::lock_guard<std::mutex> lock(m_syncMutex);
    m_activeSyncLobbies.erase(
        std::remove_if(m_activeSyncLobbies.begin(), m_activeSyncLobbies.end(),
                       [&](const std::shared_ptr<LobbyObserver> &obs)
                       {
                           return obs->getLobbyName() == lobbyName;
                       }),
        m_activeSyncLobbies.end());
}

bool AppFacade::unhideLobbyIfExists(const std::string &lobbyName)
{
    if (m_lobbyStore)
    {
        auto lobby = m_lobbyStore->getLobby(lobbyName);
        if (lobby != nullptr && lobby->getMetadata().is_hidden)
        {
            nlohmann::json patch = {{"is_hidden", false}};
            m_lobbyStore->asyncUpdateLobbyData(lobbyName, patch);
            return true;
        }
    }
    return false;
}
