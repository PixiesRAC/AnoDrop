#include "core/HandshakeManager.h"
#include "lobby/LobbyData.h"
#include "lobby/LobbyStore.h"
#include "crypto/CryptoUtils.h"
#include "network/DhtManager.h"
#include "utils/Util.h"

#include <cstring>
#include <libtorrent/hasher.hpp>
#include <libtorrent/kademlia/ed25519.hpp>
#include <random>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <expected>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <tuple>

HandshakeManager::ChannelKeys::ChannelKeys(const std::vector<unsigned char> &seed)
{
    if (seed.size() != 32)
    {
        throw std::invalid_argument("Seed must be exactly 32 bytes for Ed25519");
    }

    std::array<char, 32> lt_seed;
    std::memcpy(lt_seed.data(), seed.data(), 32);

    lt::dht::public_key pk;
    lt::dht::secret_key sk;
    std::tie(pk, sk) = lt::dht::ed25519_create_keypair(lt_seed);

    publicKey = pk.bytes;
    privateKey = sk.bytes;

    pubKeyHex = Util::toHex(publicKey);

    lt::hasher h(publicKey.data(), publicKey.size());
    lt::sha1_hash targetID = h.final();
    targetIDHex = Util::toHex(targetID.to_string());
}

nlohmann::json HandshakeManager::resolvePayload(const nlohmann::json &rawPayload, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore)
{
    nlohmann::json mapped;

    if (!rawPayload.is_array() || !lobbyStore)
    {
        return mapped;
    }

    auto observer = lobbyStore->getLobby(lobbyName);
    if (!observer)
        return mapped;
    LobbyData currentLobby = observer->getLobbyData();

    if (currentLobby.status == LobbyStatus::HANDSHAKE_DONE)
    {
        SPDLOG_INFO("Lobby {} is already HANDSHAKE_DONE. Ignoring payload.", lobbyName);
        return mapped;
    }

    SPDLOG_INFO("resolvePayload: Raw JSON content before processing for lobby {}: {}", lobbyName, rawPayload.dump());

    for (const auto &peerJson : rawPayload)
    {
        if (peerJson.is_object())
        {
            try
            {
                auto peer = peerJson.get<PeerInfo>();
                std::string incomingPub = peer.x25519_pub;

                if (!incomingPub.empty() && incomingPub != currentLobby.my_x25519_pub)
                {
                    SPDLOG_INFO("Found a peer's public key for lobby {}. Mapping to "
                                "announced_x25519_pub.",
                                lobbyName);
                    mapped["other_pseudo"] = peer.pseudo;
                    mapped["announced_x25519_pub"] = incomingPub;
                    mapped["status"] = LobbyStatus::HANDSHAKE_DONE;

                    // Perform Diffie-Hellman Key Exchange to derive shared secret
                    auto secret = deriveSharedSecret(currentLobby.my_x25519_priv, incomingPub, lobbyName);
                    if (secret)
                    {
                        mapped["private_node_discussion"] = *secret;
                    }
                    break; // Stop at the first peer that is not us
                }
                else if (incomingPub == currentLobby.my_x25519_pub)
                {
                    SPDLOG_INFO("Incoming public key is our own key for lobby {}. "
                                "Ignoring to prevent self-mapping.",
                                lobbyName);
                }
            }
            catch (const std::exception &e)
            {
                SPDLOG_WARN("Failed to parse PeerInfo for lobby {}: {}", lobbyName, e.what());
            }
        }
    }

    return mapped;
}

std::expected<std::string, std::string> HandshakeManager::deriveSharedSecret(const std::string &myPrivHex, const std::string &peerPubHex, const std::string &lobbyName)
{
    try
    {
        if (!myPrivHex.empty() && !peerPubHex.empty())
        {
            std::vector<unsigned char> myPrivRaw = Util::fromHex(myPrivHex);
            std::vector<unsigned char> peerPubRaw = Util::fromHex(peerPubHex);
            auto sharedSecretRes = CryptoUtils::deriveX25519SharedSecret(myPrivRaw, peerPubRaw);
            if (!sharedSecretRes)
            {
                SPDLOG_WARN("Failed to derive X25519 shared secret for lobby {}: {}", lobbyName, sharedSecretRes.error());
                return std::unexpected(sharedSecretRes.error());
            }
            std::vector<unsigned char> sharedSecret = *sharedSecretRes;
            std::string hexSecret = Util::toHex(sharedSecret);
            SPDLOG_INFO("Successfully derived X25519 shared secret for lobby {}. Secret: {}", lobbyName, hexSecret);
            return hexSecret;
        }
        else
        {
            SPDLOG_WARN("Cannot derive shared secret: missing keys for lobby {}", lobbyName);
            return std::unexpected("Missing keys for lobby " + lobbyName);
        }
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("Failed to derive shared secret for lobby {}: {}", lobbyName, e.what());
        return std::unexpected(std::string("Exception deriving secret: ") + e.what());
    }
}

std::string HandshakeManager::mergeHandshakePayloads(const nlohmann::json &oldDecrypted, const nlohmann::json &myPeerInfo, const std::string &lobbyName)
{
    std::string myPub = myPeerInfo["x25519_pub"].get<std::string>();

    // If the payload is completely corrupted, overwrite it.
    if (!oldDecrypted.is_array())
    {
        SPDLOG_WARN("Old DHT payload for lobby {} is not an array. Overwriting.", lobbyName);
        nlohmann::json merged = nlohmann::json::array({myPeerInfo});
        return DhtManager::encryptDhtHandshakePayload(merged, lobbyName);
    }

    bool alreadyPresent = false;
    nlohmann::json validPeers = nlohmann::json::array();

    for (const auto &peer : oldDecrypted)
    {
        if (peer.is_object() && peer.contains("x25519_pub") && peer["x25519_pub"].is_string())
        {
            validPeers.push_back(peer);
            if (peer["x25519_pub"].get<std::string>() == myPub)
            {
                alreadyPresent = true;
            }
        }
    }

    if (alreadyPresent)
    {
        if (validPeers.size() >= 2)
        {
            // Both peers are in the payload — handshake is complete.
            SPDLOG_INFO("Both peers found for lobby {}. Handshake complete.", lobbyName);
            return DhtManager::encryptDhtHandshakePayload(validPeers, lobbyName);
        }
        else
        {
            // I'm alone in the payload — CSMA/CD: rewrite to bump seqNum
            SPDLOG_INFO("My pubkey is already in the DHT item for lobby {}, lets start "
                        "the CSMA/CD",
                        lobbyName);
            return DhtManager::encryptDhtHandshakePayload(validPeers, lobbyName);
        }
    }
    else
    {
        validPeers.push_back(myPeerInfo);
        SPDLOG_INFO("Appending my pubkey to the existing DHT peers list for lobby {}.", lobbyName);
        return DhtManager::encryptDhtHandshakePayload(validPeers, lobbyName);
    }
}

nlohmann::json HandshakeManager::generateFakePeersArray()
{
    nlohmann::json fakeArray = nlohmann::json::array();
    const char hexChars[] = "0123456789abcdef";
    const char alphaNumChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> len_dist(5, 12);
    std::uniform_int_distribution<> char_dist(0, 61);
    std::uniform_int_distribution<> hex_dist(0, 15);

    for (int i = 0; i < 2; ++i)
    {
        nlohmann::json p;
        int pseudoLen = len_dist(gen);
        std::string fakePseudo;
        for (int k = 0; k < pseudoLen; ++k)
        {
            fakePseudo += alphaNumChars[char_dist(gen)];
        }
        p["pseudo"] = fakePseudo;

        std::string fakePub;
        for (int j = 0; j < 64; ++j)
        {
            fakePub += hexChars[hex_dist(gen)];
        }
        p["x25519_pub"] = fakePub;
        fakeArray.push_back(p);
    }
    return fakeArray;
}
