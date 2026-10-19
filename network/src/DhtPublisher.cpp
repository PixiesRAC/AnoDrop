#include "DhtPublisher.h"
#include "DhtAddress.h"
#include "HandshakeCoordinator.h"
#include "OutboxPublisher.h"
#include "MessageReceiver.h"
#include <spdlog/spdlog.h>

DhtPublisher::DhtPublisher(std::shared_ptr<LobbyStore> store, std::unique_ptr<DhtManager> manager, int fetchBudget)
    : m_lobbyStore(std::move(store)), m_dhtManager(std::move(manager))
{
    if (!m_lobbyStore || !m_dhtManager) throw std::invalid_argument("Missing network dependencies");
    if (fetchBudget < 1 || fetchBudget > 64) throw std::invalid_argument("Invalid fetch limit");
    m_dhtManager->setFetchLimit(static_cast<std::size_t>(fetchBudget));
    m_handshakeCoordinator = std::make_unique<HandshakeCoordinator>(*m_dhtManager, *m_lobbyStore, m_networkWorker);
    m_outboxPublisher = std::make_unique<OutboxPublisher>(*m_dhtManager, *m_lobbyStore, m_networkWorker);
    m_messageReceiver = std::make_unique<MessageReceiver>(*m_dhtManager, *m_lobbyStore, m_networkWorker, fetchBudget);
}

DhtPublisher::~DhtPublisher()
{
    shutdown();
}

void DhtPublisher::shutdown()
{
    m_ownerThread.requireOwner();
    // Shut down the thread pool FIRST. This cancels pending tasks and waits for
    // running tasks to finish. It prevents Use-After-Free of any captured
    // references (e.g. from LobbyFetcher handlers).
    m_networkWorker.shutdown();

    // Explicitly shut down alert thread and callbacks in DhtManager
    if (m_dhtManager)
    {
        m_dhtManager->shutdown();
    }

    // Explicitly destroy the DhtManager (and its alert thread) AFTER
    // so no active network tasks try to access a destroyed libtorrent session.
    m_messageReceiver.reset();
    m_outboxPublisher.reset();
    m_handshakeCoordinator.reset();
    m_dhtManager.reset();
}

void DhtPublisher::enqueueChatMessage(const struct ChatPublishParams &params)
{
    enqueueNetworkTask(params.lobbyName, [this, params] { m_outboxPublisher->publishChatMessage(params); });
}

HandshakeKeyService::ChannelKeys DhtPublisher::deriveChannelKeys(const std::string &passphrase, const std::string &lobby)
{
    auto keys = network::DhtAddress::keys(passphrase, lobby.empty() ? passphrase : lobby);
    SPDLOG_DEBUG("[DHT] Public Key (Hex): {}", keys.pubKeyHex);
    SPDLOG_DEBUG("[DHT] Target ID (SHA-1): {}", keys.targetIDHex);
    return keys;
}

void DhtPublisher::dispatchHandshakeMessage(const std::string &lobbyName, const std::string &pseudo)
{
    if (!m_handshakeCoordinator) throw std::runtime_error("Network publisher is stopped");
    m_handshakeCoordinator->dispatchHandshake(lobbyName, pseudo);
}

bool DhtPublisher::fetchLatestSequence(std::shared_ptr<LobbyObserver> lobby)
{
    return m_messageReceiver && m_messageReceiver->fetchLatestSequence(std::move(lobby));
}

void DhtPublisher::enqueueNetworkTask(const std::string& lobbyName, std::function<void()> task)
{
    m_networkWorker.post(lobbyName, std::move(task));
}

bool DhtPublisher::enqueuePollingTask(const std::string& lobby, const std::string& purpose,
                                      std::function<void()> task)
{
    const auto key = lobby + std::string(1, char(0)) + purpose;
    {
        std::lock_guard lock(m_pollMutex);
        if (!m_pendingPolls.insert(key).second) return false;
    }
    try {
        if (m_networkWorker.enqueue(lobby, [this, key, task = std::move(task)] {
            try { task(); }
            catch (...) {
                std::lock_guard lock(m_pollMutex);
                m_pendingPolls.erase(key);
                throw;
            }
            std::lock_guard lock(m_pollMutex);
            m_pendingPolls.erase(key);
        })) return true;
    } catch (...) {
        std::lock_guard lock(m_pollMutex);
        m_pendingPolls.erase(key);
        throw;
    }
    std::lock_guard lock(m_pollMutex);
    m_pendingPolls.erase(key);
    return false;
}
