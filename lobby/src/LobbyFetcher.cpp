#include "LogEvents.h"
#include "LobbyFetcher.h"

#include <random>
#include <spdlog/spdlog.h>

#include "LobbyDataSerializer.h"
#include "Util.h"

LobbyFetcher::LobbyFetcher(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, int fetchInterval)
    : m_context(std::make_shared<Context>(dhtPublisher, lobbyStore)), m_fetchInterval(fetchInterval)
{
    if (!dhtPublisher || !lobbyStore || fetchInterval <= 0)
        throw std::invalid_argument("LobbyFetcher requires dependencies and a positive interval");
}

LobbyFetcher::~LobbyFetcher()
{
    stop();
}

void LobbyFetcher::ProcessLobbies()
{
    m_ownerThread.requireOwner();
    if (m_fetchThread.joinable())
        return;
    m_fetchThread = std::jthread([this, stoken = m_stopSource.get_token()]
                                 { fetchLoop(stoken); });
}

void LobbyFetcher::stop()
{
    m_ownerThread.requireOwner();
    requestStop();
    if (m_fetchThread.joinable())
    {
        m_fetchThread.join();
    }
}

// Polling loop implementing CSMA/CA (Carrier Sense Multiple Access with Collision Avoidance)
// to prevent split-brain rendezvous collisions on shared BEP44 DHT slots.
void LobbyFetcher::fetchLoop(std::stop_token stoken)
{
    while (!stoken.stop_requested())
    {
        try
        {
            if (!m_context || !m_context->lobbyStore)
                break;

            auto lobbies = m_context->lobbyStore->getLobbies();
            auto now = std::chrono::steady_clock::now();

            for (const auto &lobby : lobbies)
            {
                std::string lobbyName = lobby->getLobbyName();
                LobbyStatus status = LobbyStatus::PENDING_HANDSHAKE;
                bool isHidden = false;

                // CSMA/CA Deterministic Offset: derives an asymmetric base delay from the X25519
                // public key prefix to ensure peers never share identical polling baselines.
                int personalOffset = 0;

                lobby->withDataView([&](const LobbyMetadata &metadata, const LobbySyncState &, const LobbyCryptoVault &crypto)
                                    {
                    status = metadata.status;
                    isHidden = metadata.is_hidden;
                    if (crypto.my_x25519_pub.length() >= 2)
                    {
                        auto prefixBytes = Util::fromHex(std::string_view(crypto.my_x25519_pub).substr(0, 2));
                        if (prefixBytes.size() == 1)
                        {
                            personalOffset = static_cast<int>(prefixBytes[0]) * 12;
                        }
                    } });

                if (isHidden)
                {
                    m_nextHandshakeAttempt.erase(lobbyName);
                    continue;
                }

                if (status == LobbyStatus::PENDING_HANDSHAKE || status == LobbyStatus::CONFIRMATION_PENDING)
                {
                    bool scheduleNext = false;
                    bool executeNow = false;

                    auto attemptIt = m_nextHandshakeAttempt.find(lobbyName);
                    if (attemptIt == m_nextHandshakeAttempt.end())
                    {
                        scheduleNext = true;
                    }
                    else if (now >= attemptIt->second)
                    {
                        scheduleNext = true;
                        executeNow = true;
                    }

                    if (scheduleNext)
                    {
                        // CSMA/CA Random Contention Window (Jitter): [0, 20s] uniform backoff to break
                        // lockstep PUT collisions and avoid split-brain states across DHT storage nodes.
                        static thread_local std::mt19937 gen(std::random_device{}());
                        std::uniform_int_distribution<> jitterDist(0, 20000);
                        const int totalDelay = m_fetchInterval + personalOffset + jitterDist(gen);

                        if (executeNow && m_context->dhtPublisher)
                        {
                            bool enqueued = m_context->dhtPublisher->enqueuePollingTask(
                                lobbyName, "status", [weakCtx = std::weak_ptr<Context>(m_context), name = lobbyName]()
                                {
                                    if (auto ctx = weakCtx.lock())
                                        ctx->processLobby(name);
                                });
                            if (enqueued)
                            {
                                m_nextHandshakeAttempt[lobbyName] = now + std::chrono::milliseconds(totalDelay);
                                SPDLOG_INFO("[LOBBY] LobbyFetcher: Executing handshake for {} (Next attempt in {} ms)", LogEvents::lobbyId(lobbyName), totalDelay);
                            }
                            else
                            {
                                SPDLOG_WARN("[LOBBY] LobbyFetcher: Handshake task for {} refused by publisher, will retry", LogEvents::lobbyId(lobbyName));
                            }
                        }
                        else
                        {
                            m_nextHandshakeAttempt[lobbyName] = now + std::chrono::milliseconds(totalDelay);
                            SPDLOG_INFO("[LOBBY] LobbyFetcher: Scheduled initial handshake for {}. First attempt in {} ms", LogEvents::lobbyId(lobbyName), totalDelay);
                        }
                    }
                }
                else
                {
                    m_nextHandshakeAttempt.erase(lobbyName);
                    if (m_context->dhtPublisher)
                    {
                        std::string taskName = lobbyName;
                        m_context->dhtPublisher->enqueuePollingTask(
                            taskName, "status", [weakCtx = std::weak_ptr<Context>(m_context), name = std::move(lobbyName)]()
                            {
                                if (auto ctx = weakCtx.lock())
                                    ctx->processLobby(name);
                            });
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            SPDLOG_ERROR("[LOBBY] Polling iteration failed: {}", e.what());
        }
        catch (...)
        {
            SPDLOG_ERROR("[LOBBY] Polling iteration threw a non-standard exception");
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
        return;

    LobbyData currentLobby = observer->getLobbyDataWithoutHistory();
    if (currentLobby.metadata.is_hidden)
        return;

    switch (currentLobby.metadata.status)
    {
    case LobbyStatus::CONFIRMATION_PENDING:
    case LobbyStatus::PENDING_HANDSHAKE:
        dhtPublisher->dispatchHandshakeMessage(lobbyName, currentLobby.metadata.my_pseudo);
        break;
    case LobbyStatus::HANDSHAKE_DONE:
    case LobbyStatus::ANOMALY:
    {
        // Each operation owns its deadline. A local refusal must not delay the others.
        const auto checkWhenDue = [&](StatusOperation kind, auto &&operation)
        {
            const auto slot = static_cast<std::size_t>(kind);
            const auto now = std::chrono::steady_clock::now();
            std::chrono::steady_clock::time_point previous;
            {
                std::lock_guard lock(statusCheckMutex);
                auto &last = lastStatusCheck[lobbyName][slot];
                if (last != std::chrono::steady_clock::time_point{} && now - last < std::chrono::seconds(STATUS_CHECK_INTERVAL_SEC))
                    return;
                previous = last;
                last = now;
            }
            bool accepted = false;
            try
            {
                accepted = operation();
            }
            catch (const std::exception &error)
            {
                SPDLOG_WARN("[LOBBY] Status polling failed for {}: {}", LogEvents::lobbyId(lobbyName), error.what());
            }
            catch (...)
            {
                SPDLOG_WARN("[LOBBY] Status polling failed for {}: non-standard exception", LogEvents::lobbyId(lobbyName));
            }
            if (!accepted)
            {
                std::lock_guard lock(statusCheckMutex);
                auto &last = lastStatusCheck.at(lobbyName)[slot];
                if (last == now)
                    last = previous;
            }
        };
        checkWhenDue(StatusOperation::AnomalyRead, [&]
                     { return anomalyHandler.checkLobbyAnomaly(lobbyName, lobbyStore); });
        checkWhenDue(StatusOperation::HeartbeatWrite, [&]
                     { heartbeatHandler.dispatchHeartbeatMessage(currentLobby); return true; });
        checkWhenDue(StatusOperation::HeartbeatRead, [&]
                     { return heartbeatHandler.checkLobbyHeartbeat(currentLobby, lobbyStore); });

        ackHandler.pushAcks(currentLobby);
        ackHandler.fetchPeerAcks(currentLobby, lobbyStore);
        break;
    }
    default:
        break;
    }
}

void LobbyFetcher::requestStop()
{
    m_stopSource.request_stop();
    m_loopCv.notify_all();
}
