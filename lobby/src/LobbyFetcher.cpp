#include "LobbyDataSerializer.h"
#include "LobbyFetcher.h"

#include <spdlog/spdlog.h>

LobbyFetcher::LobbyFetcher(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, int fetchInterval)
    : m_context(std::make_shared<Context>(dhtPublisher, lobbyStore)), m_fetchInterval(fetchInterval)
{
}

LobbyFetcher::~LobbyFetcher()
{
    stop();
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
    m_loopCv.notify_all();
    if (m_fetchThread.joinable())
    {
        m_fetchThread.join();
    }
}

void LobbyFetcher::fetchLoop(std::stop_token stoken)
{
    while (!stoken.stop_requested())
    {
        if (!m_context || !m_context->lobbyStore)
            break;

        auto lobbies = m_context->lobbyStore->getLobbies();
        auto now = std::chrono::steady_clock::now();

        for (const auto &lobby : lobbies)
        {
            std::string lobbyName = lobby->getLobbyName();
            std::string taskName = lobbyName;

            if (lobby->getStatus() == LobbyStatus::PENDING_HANDSHAKE)
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
                    lobby->withDataView([&personal_offset](const LobbyMetadata &, const LobbySyncState &, const LobbyCryptoVault &crypto) {
                        if (crypto.my_x25519_pub.length() >= 2)
                        {
                            try {
                                int firstByte = std::stoi(crypto.my_x25519_pub.substr(0, 2), nullptr, 16);
                                personal_offset = firstByte * 12;
                            } catch (...) {}
                        }
                    });
                    int total_delay = m_fetchInterval + personal_offset + jitter;
                    m_nextHandshakeAttempt[lobbyName] = now + std::chrono::milliseconds(total_delay);

                    if (executeNow && m_context->dhtPublisher)
                    {
                        SPDLOG_INFO("LobbyFetcher: Executing handshake for '{}' (Next attempt in {} ms)", lobbyName, total_delay);
                        m_context->dhtPublisher->enqueueNetworkTask(taskName, [weakCtx = std::weak_ptr<Context>(m_context), name = std::move(lobbyName)]() {
                            auto ctx = weakCtx.lock();
                            if (ctx)
                                ctx->processLobby(name);
                        });
                    }
                    else
                    {
                        SPDLOG_INFO("LobbyFetcher: Scheduled initial handshake for '{}'. First attempt in {} ms", lobbyName, total_delay);
                    }
                }
            }
            else if (m_context->dhtPublisher)
            {
                m_context->dhtPublisher->enqueueNetworkTask(taskName, [weakCtx = std::weak_ptr<Context>(m_context), name = std::move(lobbyName)]() {
                    auto ctx = weakCtx.lock();
                    if (ctx)
                        ctx->processLobby(name);
                });
            }
        }

        std::unique_lock<std::mutex> cv_lock(m_loopMutex);
        m_loopCv.wait_for(cv_lock, stoken, std::chrono::milliseconds(ESTABLISHED_LOBBY_FETCH_INTERVAL_MS), []
                    { return false; });
    }
}

void LobbyFetcher::Context::processLobby(const std::string &lobbyName)
{
    if (!lobbyStore || !dhtPublisher)
        return;

    auto observer = lobbyStore->getLobby(lobbyName);

    if (!observer)
    {
        return;
    }
    if (observer->getMetadata().is_hidden)
    {
        return;
    }

    LobbyStatus currentStatus = observer->getStatus();
    switch (currentStatus)
    {
    case LobbyStatus::PENDING_HANDSHAKE:
        dhtPublisher->dispatchHandshakeMessage(lobbyName, observer->getMetadata().my_pseudo);
        break;
    case LobbyStatus::HANDSHAKE_DONE:
    case LobbyStatus::ANOMALY:
    {
        LobbyData currentLobby = observer->getLobbyDataWithoutHistory();
        auto now = std::chrono::steady_clock::now();
        bool shouldCheck = false;
        {
            std::lock_guard<std::mutex> lock(statusCheckMutex);
            if (lastStatusCheck.find(lobbyName) == lastStatusCheck.end() || std::chrono::duration_cast<std::chrono::seconds>(now - lastStatusCheck[lobbyName]).count() >= STATUS_CHECK_INTERVAL_SEC)
            {
                lastStatusCheck[lobbyName] = now;
                shouldCheck = true;
            }
        }

        if (shouldCheck)
        {
            anomalyHandler.checkLobbyAnomaly(lobbyName, lobbyStore);
            heartbeatHandler.dispatchHeartbeatMessage(currentLobby);
            heartbeatHandler.checkLobbyHeartbeat(currentLobby, lobbyStore);
        }

        ackHandler.pushAcks(currentLobby);
        ackHandler.fetchPeerAcks(currentLobby, lobbyStore);
        break;
    }
    default:
        break;
    }
}
