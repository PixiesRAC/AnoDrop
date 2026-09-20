#include "lobby/HandshakeStateEngine.h"
#include "crypto/HandshakePayloadCodec.h"
#include "crypto/HandshakeKeyService.h"
#include "crypto/CryptoUtils.h"
#include "lobby/FakeTimezonePool.h"
#include "lobby/LobbyData.h"
#include "lobby/LobbyStore.h"
#include "utils/Util.h"

#include <spdlog/spdlog.h>
#include <random>

std::optional<std::vector<unsigned char>> HandshakeStateEngine::prepareLobbyPayload(std::shared_ptr<LobbyStore> lobbyStore, const std::string &lobbyName, const std::string &pseudo)
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
        patch["my_seq_num"] = 0;
        patch["announced_seq_num"] = 0;

        FakeTimezone tz = FakeTimezonePool::pickRandom();
        patch["fake_timezone"] = tz;
    }

    patch["my_pseudo"] = pseudo;

    lobbyStore->asyncUpdateLobbyData(lobbyName, patch);

    std::string final_pub = lobbyData.my_x25519_pub.empty() ? patch["my_x25519_pub"].get<std::string>() : lobbyData.my_x25519_pub;
    FakeTimezone final_tz = lobbyData.fake_timezone.name.empty() ? patch["fake_timezone"].get<FakeTimezone>() : lobbyData.fake_timezone;

    PeerInfo peerInfo{pseudo, final_pub, final_tz};
    nlohmann::json payload = nlohmann::json::array({peerInfo});

    auto encryptedRes = HandshakePayloadCodec::encryptDhtHandshakePayload(payload, lobbyName);
    if (!encryptedRes) {
        SPDLOG_ERROR("prepareLobbyPayload: encryption failed: {}", encryptedRes.error());
        return std::nullopt;
    }
    return *encryptedRes;
}

nlohmann::json HandshakeStateEngine::resolvePayload(const nlohmann::json &rawPayload, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore)
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
        return mapped;
    }

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
                    mapped["other_pseudo"] = peer.pseudo;
                    mapped["announced_x25519_pub"] = incomingPub;
                    mapped["status"] = LobbyStatus::HANDSHAKE_DONE;

                    if (!peer.fake_timezone.name.empty())
                    {
                        mapped["peer_fake_timezone"] = peer.fake_timezone;
                    }

                    auto secret = HandshakeKeyService::deriveSharedSecret(currentLobby.my_x25519_priv, incomingPub, lobbyName);
                    if (secret)
                    {
                        mapped["private_node_discussion"] = *secret;
                    }
                    break;
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

std::expected<std::vector<unsigned char>, std::string> HandshakeStateEngine::mergeHandshakePayloads(const nlohmann::json &oldDecrypted, const nlohmann::json &myPeerInfo, const std::string &lobbyName)
{
    std::string myPub = myPeerInfo["x25519_pub"].get<std::string>();

    if (!oldDecrypted.is_array())
    {
        nlohmann::json merged = nlohmann::json::array({myPeerInfo});
        return HandshakePayloadCodec::encryptDhtHandshakePayload(merged, lobbyName);
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

    if (!alreadyPresent)
    {
        validPeers.push_back(myPeerInfo);
    }
    
    return HandshakePayloadCodec::encryptDhtHandshakePayload(validPeers, lobbyName);
}

nlohmann::json HandshakeStateEngine::generateFakePeersArray()
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
        p["fake_timezone"] = FakeTimezonePool::pickRandom();
        fakeArray.push_back(p);
    }
    return fakeArray;
}
