#include "lobby/LobbyFetcher.h"
#include <chrono>
#include <random>
#include <spdlog/spdlog.h>

LobbyFetcher::LobbyFetcher(std::shared_ptr<DhtPublisher> dhtPublisher, std::shared_ptr<LobbyStore> lobbyStore, int fetchInterval) : m_lobbyStore(lobbyStore), m_fetchInterval(fetchInterval), m_dhtPublisher(dhtPublisher)
{
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
    while (!stoken.stop_requested())
    {
        auto lobbies = m_lobbyStore->getLobbies();

        for (const auto &lobby : lobbies)
        {
            processLobby(lobby->getLobbyName());
        }

        // CSMA/CD logic: add a random jitter (0 to 20 seconds) to avoid lockstep collisions
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> jitter_dist(0, 20000);
        int jitter = jitter_dist(gen);

        // Identity-based offset (acting like a hash of the user's identity/password derived key)
        int personal_offset = 0;
        if (!lobbies.empty() && lobbies[0]->getLobbyData().my_x25519_pub.length() >= 2)
        {
            // Parse the first byte (2 hex chars) of the public key (0 to 255)
            // Multiplying by 12 gives a fixed staggered offset between 0 and 3060ms
            int firstByte = std::stoi(lobbies[0]->getLobbyData().my_x25519_pub.substr(0, 2), nullptr, 16);
            personal_offset = firstByte * 12;
        }
        SPDLOG_INFO("LobbyFetcher fetchLoop sleeping for {} ms ({} base + {} personal + {} jitter)", m_fetchInterval + personal_offset + jitter, m_fetchInterval, personal_offset, jitter);
        std::this_thread::sleep_for(std::chrono::milliseconds(m_fetchInterval + personal_offset + jitter));
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
        m_dhtPublisher->waitForPutConfirmed(currentLobby.lobby_name);
        break;
    case LobbyStatus::HANDSHAKE_DONE:
    {
        auto now = std::chrono::steady_clock::now();
        if (m_lastHandshakePoll.find(lobbyName) == m_lastHandshakePoll.end() ||
            std::chrono::duration_cast<std::chrono::seconds>(now - m_lastHandshakePoll[lobbyName]).count() >= 30)
        {
            m_lastHandshakePoll[lobbyName] = now;
            m_dhtPublisher->checkLobbyAnomaly(lobbyName, m_lobbyStore);
        }
        break;
    }
    case LobbyStatus::ANOMALY:
        SPDLOG_WARN("Lobby {} has an ANOMALY - Placeholder", currentLobby.lobby_name);
        break;
    default:
        break;
    }
}
