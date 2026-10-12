#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "HandshakeStateEngine.h"

#include <spdlog/spdlog.h>

#include "CryptoUtils.h"
#include "HandshakeKeyService.h"
#include "HandshakePayloadCodec.h"
#include "LobbyStore.h"
#include "Util.h"

std::optional<std::vector<unsigned char>> HandshakeStateEngine::prepareLobbyPayload(std::shared_ptr<LobbyStore> lobbyStore, const std::string &lobbyName, const std::string &pseudo)
{
    auto observer = lobbyStore->getOrCreateLobby(lobbyName);
    LobbyData lobbyData = observer->getLobbyDataWithoutHistory(); // If no X25519 key exists, generate one

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
    if (!encryptedRes)
    {
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
    LobbyData currentLobby = observer->getLobbyDataWithoutHistory();

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

                auto isValidHex = [](const std::string &s)
                {
                    if (s.length() != 64)
                        return false;
                    return std::all_of(s.begin(), s.end(), [](unsigned char c)
                                       { return std::isxdigit(c); });
                };

                if (!incomingPub.empty() && incomingPub != currentLobby.my_x25519_pub && isValidHex(incomingPub))
                {
                    auto secret = HandshakeKeyService::deriveSharedSecret(currentLobby.my_x25519_priv, incomingPub, lobbyName);
                    if (secret)
                    {
                        mapped["other_pseudo"] = peer.pseudo;
                        mapped["announced_x25519_pub"] = incomingPub;
                        mapped["status"] = LobbyStatus::HANDSHAKE_DONE;

                        if (!peer.fake_timezone.name.empty())
                        {
                            mapped["peer_fake_timezone"] = peer.fake_timezone;
                        }
                        mapped["private_node_discussion"] = *secret;
                        break; // Still breaks for now, but avoids junk strings
                    }
                    else
                    {
                        SPDLOG_ERROR("Cryptographic derivation failed for lobby {}", lobbyName);
                    }
                }
                else if (!isValidHex(incomingPub) && !incomingPub.empty())
                {
                    SPDLOG_WARN("Invalid public key format received in handshake (must be 64 hex chars).");
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

    static thread_local std::mt19937 gen(std::random_device{}());
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

HandshakeTransitionResult HandshakeStateEngine::processHandshakeTransition(
    const std::string &ourEncryptedPayload,
    const std::string &lobbyName,
    std::shared_ptr<LobbyStore> lobbyStore,
    std::int64_t currentDhtSeqNum,
    const std::string &existingItemString,
    bool hasExistingItem)
{
    auto ourDecryptedPayload = HandshakePayloadCodec::decryptDhtHandShakePayload(ourEncryptedPayload, lobbyName);
    if (!ourDecryptedPayload || !ourDecryptedPayload->is_array() || ourDecryptedPayload->empty())
    {
        SPDLOG_ERROR("Dropper callback: our own payload is invalid");
        return {false, "", 0};
    }

    auto ourPeerInfo = (*ourDecryptedPayload)[0];

    bool isHandshakeDone = false;
    std::string finalEncryptedPayload = ourEncryptedPayload;
    std::int64_t seqNum = currentDhtSeqNum;

    if (hasExistingItem && seqNum > 0)
    {
        if (seqNum >= INT64_MAX - 2)
        {
            SPDLOG_INFO("Handshake is done for both parties of lobby {}", lobbyName);
            isHandshakeDone = true;
        }
        auto existingDecryptedPayload = HandshakePayloadCodec::decryptDhtHandShakePayload(existingItemString, lobbyName);

        if (!existingDecryptedPayload)
        {
            SPDLOG_WARN("Lobby {} payload is corrupted. Set to ANOMALY.", lobbyName);
            if (lobbyStore)
                lobbyStore->asyncUpdateLobbyData(lobbyName, {{"status", LobbyStatus::ANOMALY}});
            return {false, "", 0};
        }
        if (lobbyStore)
        {
            nlohmann::json resolvedPatch = resolvePayload(*existingDecryptedPayload, lobbyName, lobbyStore);
            if (!resolvedPatch.empty())
            {
                // Timezone adoption logic:
                // The person who set SEQ_MAX first (the initiator) keeps their own timezone.
                // The late participant (who sees isHandshakeDone = true) adopts the initiator's timezone.
                if (isHandshakeDone && resolvedPatch.contains("peer_fake_timezone"))
                {
                    FakeTimezone peerTz = resolvedPatch["peer_fake_timezone"].get<FakeTimezone>();
                    resolvedPatch["fake_timezone"] = peerTz;
                    SPDLOG_INFO("Adopting peer's fake timezone '{}' for lobby {} (they set SEQ_MAX first)", peerTz.name, lobbyName);
                }

                // Remove peer_fake_timezone from the patch — it's transient metadata, not a lobby field
                resolvedPatch.erase("peer_fake_timezone");

                lobbyStore->asyncUpdateLobbyData(lobbyName, resolvedPatch);

                // A finalized DHT item must retain its signed sequence even when
                // this callback has only just completed the local handshake.
                if (!isHandshakeDone && resolvedPatch.contains("status") && resolvedPatch["status"].get<int>() == static_cast<int>(LobbyStatus::HANDSHAKE_DONE))
                {
                    SPDLOG_INFO("Handshake complete for lobby {}. Setting seqNum to "
                                "INT64_MAX - 1",
                                lobbyName);
                    seqNum = INT64_MAX - 2;
                }
            }
        }

        if (isHandshakeDone)
        {
            SPDLOG_INFO("Handshake is already done for lobby {}. Preserving existing DHT item.", lobbyName);
            return {false, "", 0};
        }

        auto mergeRes = mergeHandshakePayloads(*existingDecryptedPayload, ourPeerInfo, lobbyName);
        if (mergeRes)
        {
            finalEncryptedPayload = std::string(mergeRes->begin(), mergeRes->end());
        }
    }

    auto decryptedForLog = HandshakePayloadCodec::decryptDhtHandShakePayload(finalEncryptedPayload, lobbyName);
    if (decryptedForLog)
    {
        SPDLOG_INFO("Sent DHT Handshake payload (decrypted): {} for lobby {}", decryptedForLog->dump(), lobbyName);
    }

    if (finalEncryptedPayload.size() > 1000)
    {
        SPDLOG_ERROR("Merged payload for lobby {} exceeds 1000 bytes ({}). Aborting.", lobbyName, finalEncryptedPayload.size());
        return {false, "", 0};
    }

    return {true, finalEncryptedPayload, seqNum + 1};
}

