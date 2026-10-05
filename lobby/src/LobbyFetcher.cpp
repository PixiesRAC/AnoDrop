#include "LobbyDataSerializer.h"
#include "LobbyFetcher.h"

#include <spdlog/spdlog.h>

LobbyFetcher::LobbyFetcher(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, int fetchInterval)
    : m_lobbyStore(lobbyStore), m_fetchInterval(fetchInterval), m_dhtPublisher(dhtPublisher), anomalyHandler(dhtPublisher), heartbeatHandler(dhtPublisher), ackHandler(dhtPublisher)
{
}

LobbyFetcher::~LobbyFetcher()
{
    if (m_fetchThread.joinable())
    {
        m_fetchThread.request_stop();
        m_fetchThread.join();
    }
}

void LobbyFetcher::ProcessLobbies()
{
    if (m_fetchThread.joinable())
        return;
    m_fetchThread = std::jthread(&LobbyFetcher::fetchLoop, this);
}

void LobbyFetcher::stop()
{
    m_fetchThread.request_stop();
}

void LobbyFetcher::fetchLoop(std::stop_token stoken)
{
    std::mutex cv_m;
    std::condition_variable_any cv;

    while (!stoken.stop_requested())
    {
        auto lobbies = m_lobbyStore->getLobbies();
        auto now = std::chrono::steady_clock::now();

        for (const auto &lobby : lobbies)
        {
            std::string lobbyName = lobby->getLobbyName();
            std::string taskName = lobbyName;

            if (lobby->getLobbyData().status == LobbyStatus::PENDING_HANDSHAKE)
            {
                bool scheduleNext = false;
                bool executeNow = false;

                if (m_nextHandshakeAttempt.find(lobbyName) == m_nextHandshakeAttempt.end())
                {
                    scheduleNext = true;
                }
                else if (now >= m_nextHandshakeAttempt[lobbyName])
                {
                    scheduleNext = true;
                    executeNow = true;
                }

                if (scheduleNext)
                {
                    static thread_local std::mt19937 gen(std::random_device{}());
                    std::uniform_int_distribution<> jitter_dist(0, 20000);
                    int jitter = jitter_dist(gen);

                    int personal_offset = 0;
                    if (lobby->getLobbyData().my_x25519_pub.length() >= 2)
                    {
                        int firstByte = std::stoi(lobby->getLobbyData().my_x25519_pub.substr(0, 2), nullptr, 16);
                        personal_offset = firstByte * 12;
                    }
                    int total_delay = m_fetchInterval + personal_offset + jitter;
                    m_nextHandshakeAttempt[lobbyName] = now + std::chrono::milliseconds(total_delay);

                    if (executeNow)
                    {
                        SPDLOG_INFO("LobbyFetcher: Executing handshake for '{}' (Next attempt in {} ms)", lobbyName, total_delay);
                        m_dhtPublisher->enqueueNetworkTask(taskName, [this, name = std::move(lobbyName)]()
                                                           { this->processLobby(name); });
                    }
                    else
                    {
                        SPDLOG_INFO("LobbyFetcher: Scheduled initial handshake for '{}'. First attempt in {} ms", lobbyName, total_delay);
                    }
                }
            }
            else
            {
                // No mutex is necessary here or in the callback because PartitionedThreadPool
                // guarantees sequential execution for tasks with the same partition key (lobbyName).
                m_dhtPublisher->enqueueNetworkTask(taskName, [this, name = std::move(lobbyName)]()
                                                   { this->processLobby(name); });
            }
        }

        std::unique_lock<std::mutex> cv_lock(cv_m);
        cv.wait_for(cv_lock, stoken, std::chrono::milliseconds(ESTABLISHED_LOBBY_FETCH_INTERVAL_MS), []
                    { return false; });
    }
}

void LobbyFetcher::processLobby(std::string lobbyName)
{
    auto observer = m_lobbyStore->getLobby(lobbyName);

    if (!observer)
    {
        return;
    }
    LobbyData currentLobby = observer->getLobbyData();
    if (currentLobby.is_hidden)
    {
        return;
    }

    switch (currentLobby.status)
    {
    case LobbyStatus::PENDING_HANDSHAKE:
        m_dhtPublisher->dispatchHandshakeMessage(lobbyName, currentLobby.my_pseudo);
        // We do NOT block here with waitForPutHandshakeConfirmed. Blocking the worker thread
        // prevents PartitionedThreadPool from processing chat messages for other lobbies sharing this thread.
        break;
    case LobbyStatus::HANDSHAKE_DONE:
    case LobbyStatus::ANOMALY:
    {
        auto now = std::chrono::steady_clock::now();
        if (m_lastStatusCheck.find(lobbyName) == m_lastStatusCheck.end() || std::chrono::duration_cast<std::chrono::seconds>(now - m_lastStatusCheck[lobbyName]).count() >= STATUS_CHECK_INTERVAL_SEC)
        {
            m_lastStatusCheck[lobbyName] = now;
            anomalyHandler.checkLobbyAnomaly(lobbyName, m_lobbyStore);
            heartbeatHandler.dispatchHeartbeatMessage(currentLobby);
            heartbeatHandler.checkLobbyHeartbeat(currentLobby, m_lobbyStore);
        }

        ackHandler.pushAcks(currentLobby);
        ackHandler.fetchPeerAcks(currentLobby, m_lobbyStore);
        break;
    }
    default:
        break;
    }
}
