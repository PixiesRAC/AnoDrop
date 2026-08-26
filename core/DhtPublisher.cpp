#include "core/DhtPublisher.h"
#include "core/HandshakeManager.h"
#include "crypto/Hasher.h"
#include "network/DhtManager.h"
#include "network/Dropper.h"

#include <libtorrent/hasher.hpp>
#include <nlohmann/json.hpp>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

void DhtPublisher::setDhtManager(std::unique_ptr<DhtManager> dhtManager)
{
    m_dhtManager = std::move(dhtManager);
}

void DhtPublisher::setLobbyStore(std::shared_ptr<LobbyStore> lobbyStore)
{
    m_lobbyStore = lobbyStore;
}

HandshakeManager::ChannelKeys DhtPublisher::deriveChannelKeys(const std::string &passphrase)
{
    std::vector<unsigned char> seed = Hasher::getInstance().getHashMsg256(passphrase);
    HandshakeManager::ChannelKeys info(seed);

    SPDLOG_INFO("Public Key (Hex): {}", info.pubKeyHex);
    SPDLOG_INFO("Target ID (SHA-1): {}", info.targetIDHex);

    return info;
}

void DhtPublisher::dispatchHandshakeMessage(const std::string &lobbyName, const std::string &pseudo)
{
    if (!m_dhtManager)
    {
        throw std::runtime_error("DHT Manager is not initialized");
    }
    if (!m_lobbyStore)
    {
        throw std::runtime_error("Lobby Store is not initialized");
    }

    auto ciphertextOpt = DhtManager::prepareLobbyPayload(m_lobbyStore, lobbyName, pseudo);
    if (!ciphertextOpt)
    {
        SPDLOG_WARN("Skipping handshake dispatch: could not prepare payload for lobby {}", lobbyName);
        return;
    }
    const auto &ciphertext = *ciphertextOpt;

    std::string encryptedMessage(ciphertext.begin(), ciphertext.end());

    std::vector<unsigned char> seed = Hasher::getInstance().getHashMsg256(lobbyName);
    HandshakeManager::ChannelKeys info(seed);

    Dropper dropper(m_dhtManager->getSession(), info.publicKey, info.privateKey);
    dropper.dropMessage(encryptedMessage, lobbyName, m_lobbyStore);
}

void DhtPublisher::waitForPutConfirmed(const std::string &lobbyName)
{
    if (m_dhtManager)
    {
        m_dhtManager->waitForPutConfirmed(lobbyName);
    }
}
