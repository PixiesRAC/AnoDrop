#include "network/DhtManager.h"

#include <chrono>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <thread>

#include "crypto/CryptoUtils.h"
#include "crypto/Hasher.h"
#include "lobby/LobbyStore.h"

DhtManager::DhtManager() : AlertObserver()
{
    SPDLOG_INFO("Initializing DhtManager session");
    lt::settings_pack p;
    p.set_bool(lt::settings_pack::enable_dht, true);
    p.set_bool(lt::settings_pack::enable_upnp, false);
    p.set_bool(lt::settings_pack::enable_natpmp, false);
    p.set_bool(lt::settings_pack::enable_lsd, false);
    p.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);

    m_session = std::make_shared<lt::session>(p);
}

DhtManager::~DhtManager()
{
    SPDLOG_INFO("~DhtManager: destroying DHT session...");
}

void DhtManager::startAlertThread()
{
    m_alertListener = std::make_unique<AlertListener>(m_session);

    m_alertListener->addObserver(this);
    m_alertThread = std::jthread(&AlertListener::checkForAlerts, m_alertListener.get());
}

std::shared_ptr<lt::session> DhtManager::getSharedSession() const
{
    return m_session;
}

lt::session *DhtManager::getSession() const
{
    return m_session.get();
}

void DhtManager::onAlert(lt::alert *alert)
{
    if (auto *stats = lt::alert_cast<lt::dht_stats_alert>(alert))
    {
        bool is_v6 = stats->local_endpoint.address().is_v6();
        std::stringstream ss;
        ss << stats->nid;

        {
            std::lock_guard<std::mutex> lock(m_nodeIdMutex);
            if (is_v6)
            {
                m_nodeIdIpv6 = stats->nid;
                SPDLOG_INFO("Local Node ID (IPv6): {}", ss.str());
            }
            else
            {
                m_nodeIdIpv4 = stats->nid;
                SPDLOG_INFO("Local Node ID (IPv4): {}", ss.str());
            }
        }

        int total_nodes = 0;
        for (auto const &bucket : stats->routing_table)
        {
            total_nodes += bucket.num_nodes;
        }
        SPDLOG_INFO("DHT stats received ({}). Known nodes: {}", is_v6 ? "IPv6" : "IPv4", total_nodes);
        m_isDhtReady.store(true, std::memory_order_release);
        m_isDhtReady.notify_one();
    }
    else if (auto *put_alert = lt::alert_cast<lt::dht_put_alert>(alert))
    {
        std::stringstream ss;
        ss << put_alert->target;
        SPDLOG_INFO("DHT put operation finished! Target: {}", ss.str());
        m_putConfirmedSem.release();
    }
    else if (auto *get_mut = lt::alert_cast<lt::dht_mutable_item_alert>(alert))
    {
        SPDLOG_INFO("Mutable item found! Sequence: {}", get_mut->seq);
    }
    else if (auto *bootstrap = lt::alert_cast<lt::dht_bootstrap_alert>(alert))
    {
        SPDLOG_INFO("DHT bootstrap complete! The engine is connected to the network.");
        m_session->post_dht_stats();
    }
    else if (auto *listen_ok = lt::alert_cast<lt::listen_succeeded_alert>(alert))
    {
        SPDLOG_INFO("Network listening started successfully on port: {}", listen_ok->port);
    }
    else if (auto *listen_err = lt::alert_cast<lt::listen_failed_alert>(alert))
    {
        SPDLOG_ERROR("Failed to open network port: {}", listen_err->error.message());
    }
    else if (auto *dht_err = lt::alert_cast<lt::dht_error_alert>(alert))
    {
        SPDLOG_WARN("Internal DHT error: {}", dht_err->error.message());
    }
}

void DhtManager::waitUntilDhtIsReady()
{
    SPDLOG_INFO("waiting for DHT to be ready");
    m_isDhtReady.wait(false, std::memory_order_acquire);
}

void DhtManager::resetPutConfirmed()
{
    while (m_putConfirmedSem.try_acquire())
    {
    }
}

void DhtManager::waitForPutConfirmed(const std::string &lobbyName)
{
    resetPutConfirmed();
    if (!lobbyName.empty())
    {
        SPDLOG_INFO("waiting for put confirmed for {}", lobbyName);
    }
    else
    {
        SPDLOG_INFO("waiting for put confirmed");
    }
    m_putConfirmedSem.acquire();
}

bool DhtManager::isSessionReady() const
{
    if (!m_session)
        return false;
    return true;
}

std::optional<lt::sha1_hash> DhtManager::getLocalNodeId(IpProtocol protocol) const
{
    std::lock_guard<std::mutex> lock(m_nodeIdMutex);
    std::optional<lt::sha1_hash> result = (protocol == IpProtocol::IPv4) ? m_nodeIdIpv4 : m_nodeIdIpv6;
    if (result && result->is_all_zeros())
    {
        return std::nullopt;
    }
    return result;
}

std::optional<nlohmann::json> DhtManager::decryptDhtHandShakePayload(const std::string &ciphertextStr, const std::string &lobbyName)
{
    try
    {
        std::vector<unsigned char> ciphertext(ciphertextStr.begin(), ciphertextStr.end());
        std::vector<unsigned char> lobbyKey = Hasher::getInstance().getHashMsg256(lobbyName);
        auto plaintextRes = CryptoUtils::decryptAES256GCM(ciphertext, lobbyKey);
        if (!plaintextRes)
        {
            SPDLOG_WARN("Failed to decrypt DHT handshake payload for lobby {}: {}", lobbyName, plaintextRes.error());
            return std::nullopt;
        }
        std::vector<unsigned char> plaintext = *plaintextRes;
        std::string jsonStr(plaintext.begin(), plaintext.end());
        return nlohmann::json::parse(jsonStr);
    }
    catch (const std::exception &e)
    {
        SPDLOG_WARN("Failed to decrypt DHT payload for lobby {}: {}", lobbyName, e.what());
        return std::nullopt;
    }
}

std::string DhtManager::encryptDhtHandshakePayload(const nlohmann::json &payload, const std::string &lobbyName)
{
    std::string jsonStr = payload.dump();
    std::vector<unsigned char> plaintextVec(jsonStr.begin(), jsonStr.end());
    std::vector<unsigned char> lobbyKey = Hasher::getInstance().getHashMsg256(lobbyName);
    auto encryptedRes = CryptoUtils::encryptAES256GCM(plaintextVec, lobbyKey);
    if (!encryptedRes)
    {
        SPDLOG_ERROR("Failed to encrypt DHT handshake payload for lobby {}: {}", lobbyName, encryptedRes.error());
        return "";
    }
    std::vector<unsigned char> encrypted = *encryptedRes;
    return std::string(encrypted.begin(), encrypted.end());
}

std::optional<std::vector<unsigned char>> DhtManager::prepareLobbyPayload(std::shared_ptr<LobbyStore> lobbyStore, const std::string &lobbyName, const std::string &pseudo)
{
    auto observer = lobbyStore->getOrCreateLobby(lobbyName);
    LobbyData lobbyData = observer->getLobbyData(); // If no X25519 key exists, generate one

    nlohmann::json patch;
    if (lobbyData.my_x25519_pub.empty() || lobbyData.my_x25519_priv.empty())
    {
        auto keyPairRes = CryptoUtils::generateX25519KeyPair();
        if (!keyPairRes)
        {
            SPDLOG_ERROR("Failed to generate X25519 keypair for lobby {}: {}", lobbyName, keyPairRes.error());
            return std::nullopt;
        }
        CryptoUtils::X25519KeyPair keyPair = *keyPairRes;
        patch["my_x25519_pub"] = Util::toHex(keyPair.publicKey);
        patch["my_x25519_priv"] = Util::toHex(keyPair.privateKey);
        patch["status"] = LobbyStatus::PENDING_HANDSHAKE;
    }

    patch["my_pseudo"] = pseudo;

    lobbyStore->updateLobbyData(lobbyName, patch);
    lobbyData = observer->getLobbyData(); // refresh

    if (patch.contains("my_x25519_pub"))
    {
        SPDLOG_INFO("Generated new X25519 keys for lobby {}. PubKey: {}", lobbyName, lobbyData.my_x25519_pub);
    }

    SPDLOG_INFO("Lobby payload prepared for lobby {}, pseudo {}", lobbyName, lobbyData.my_pseudo);

    PeerInfo peerInfo{lobbyData.my_pseudo, lobbyData.my_x25519_pub};
    nlohmann::json payload = nlohmann::json::array({peerInfo});

    std::string encryptedStr = encryptDhtHandshakePayload(payload, lobbyName);
    return std::vector<unsigned char>(encryptedStr.begin(), encryptedStr.end());
}