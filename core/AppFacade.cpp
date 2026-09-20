#include "core/AppFacade.h"
#include "crypto/CryptoUtils.h"
#include "crypto/Hasher.h"
#include <algorithm>
#include <ratio>
#include <spdlog/spdlog.h>
#include <thread>
#include <unordered_map>

constexpr auto POLLING_TIMEOUT = std::chrono::milliseconds(5000);
constexpr auto MAX_TIMEOUT_COUNT = 50;

AppFacade::AppFacade(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore) : m_dhtPublisher(std::move(dhtPublisher)), m_lobbyStore(std::move(lobbyStore))
{
    m_syncThread = std::jthread(&AppFacade::syncThreadLoop, this);
}

AppFacade::~AppFacade()
{
    m_syncThread.request_stop();
    if (m_syncThread.joinable())
    {
        m_syncThread.join();
    }
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

    m_dhtPublisher->dispatchAnomalyMessage(lobbyName, message);
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

void AppFacade::synchronizeAndProcessNewIncomingMessage(std::shared_ptr<LobbyObserver> lobbyObs)
{
    if (!m_dhtPublisher || !m_lobbyStore)
    {
        throw std::runtime_error("Dependencies missing in AppFacade");
    }

    std::lock_guard<std::mutex> lock(m_syncMutex);
    auto it = std::find(m_activeSyncLobbies.begin(), m_activeSyncLobbies.end(), lobbyObs);
    if (it == m_activeSyncLobbies.end())
    {
        m_activeSyncLobbies.push_back(lobbyObs);
    }
}

void AppFacade::syncThreadLoop(std::stop_token stoken)
{

    std::unordered_map<std::string, int> timeoutCounters;

    while (!stoken.stop_requested())
    {
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
                LobbyData data = obs->getLobbyData();
                std::string targetKey = data.private_node_discussion + data.announced_x25519_pub;
                if (m_dhtPublisher)
                {
                    m_dhtPublisher->fetchDhtItem(targetKey, obs, true);
                    obs->setFetchingSeq();
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
        }
        std::this_thread::sleep_for(POLLING_TIMEOUT);
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
            [&](const std::shared_ptr<LobbyObserver>& obs) {
                return obs->getLobbyName() == lobbyName;
            }),
        m_activeSyncLobbies.end()
    );
}

bool AppFacade::unhideLobbyIfExists(const std::string &lobbyName)
{
    if (m_lobbyStore)
    {
        auto lobby = m_lobbyStore->getLobby(lobbyName);
        if (lobby != nullptr && lobby->getLobbyData().is_hidden)
        {
            nlohmann::json patch = {{"is_hidden", false}};
            m_lobbyStore->asyncUpdateLobbyData(lobbyName, patch);
            return true;
        }
    }
    return false;
}
