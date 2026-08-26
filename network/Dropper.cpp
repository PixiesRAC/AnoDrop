#include "network/Dropper.h"
#include "crypto/Hasher.h"
#include "utils/Util.h"

#include <functional>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/kademlia/item.hpp>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

Dropper::Dropper(lt::session *session, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey) : m_session(session), m_publicKey(publicKey), m_privateKey(privateKey)
{
}

Dropper::~Dropper()
{
    SPDLOG_INFO("~Dropper: destroying Dropper...");
}

#include "lobby/LobbyStore.h"
#include "network/DhtManager.h"

#include "core/HandshakeManager.h"

static void put_mutable_callback(const std::string &message, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey, lt::entry &itemEntry, std::array<char, 64> &itemSignature, std::int64_t &sequenceNumber, std::string const &itemSalt)
{
    auto newDecrypted = DhtManager::decryptDhtHandShakePayload(message, lobbyName);
    if (!newDecrypted || !newDecrypted->is_array() || newDecrypted->empty())
    {
        SPDLOG_ERROR("Dropper callback: our own payload is invalid");
        return;
    }

    auto myPeerInfo = (*newDecrypted)[0];
    std::string myPub = myPeerInfo["x25519_pub"].get<std::string>();

    bool shouldWrite = true;
    bool isHandshakeDone = false;
    std::string finalMessage = message;

    if (sequenceNumber > 0 && itemEntry.type() == lt::entry::string_t)
    {
        if (sequenceNumber >= INT64_MAX - 2)
        {
            SPDLOG_INFO("Handshake is done for both parties of lobby {}", lobbyName);
            isHandshakeDone = true;
        }
        auto oldDecrypted = DhtManager::decryptDhtHandShakePayload(itemEntry.string(), lobbyName);

        if (!oldDecrypted)
        {
            SPDLOG_WARN("Lobby {} payload is corrupted. Set to ANOMALY.", lobbyName);
            if (lobbyStore)
                lobbyStore->updateLobbyData(lobbyName, {{"status", LobbyStatus::ANOMALY}});
            return;
        }
        if (lobbyStore)
        {
            nlohmann::json resolvedPatch = HandshakeManager::resolvePayload(*oldDecrypted, lobbyName, lobbyStore);
            if (!resolvedPatch.empty())
            {
                lobbyStore->updateLobbyData(lobbyName, resolvedPatch);

                if (resolvedPatch.contains("status") && resolvedPatch["status"].get<int>() == static_cast<int>(LobbyStatus::HANDSHAKE_DONE))
                {
                    SPDLOG_INFO("Handshake complete for lobby {}. Setting seqNum to "
                                "INT64_MAX - 1",
                                lobbyName);
                    sequenceNumber = INT64_MAX - 2; // Will become INT64_MAX - 1 after the ++ below
                }
            }
        }

        if (!isHandshakeDone)
        {
            // 2. Merge arrays or erase
            finalMessage = HandshakeManager::mergeHandshakePayloads(*oldDecrypted, myPeerInfo, lobbyName);
        }
        else
        {
            // Write ghost payload to finally burn the slot and wipe the keys
            nlohmann::json endPayload = {{"status", "DONE"}, {"msg", "END HANDSHAKE"}};
            finalMessage = DhtManager::encryptDhtHandshakePayload(endPayload, lobbyName);
            // finalMessage = message; // Keep the same message for now
        }
    }

    auto decryptedForLog = DhtManager::decryptDhtHandShakePayload(finalMessage, lobbyName);
    if (decryptedForLog)
    {
        SPDLOG_INFO("Sent DHT Handshake payload (decrypted): {} for lobby {}", decryptedForLog->dump(), lobbyName);
    }

    if (finalMessage.size() > 1000)
    {
        SPDLOG_ERROR("Merged payload for lobby {} exceeds 1000 bytes ({}). Aborting.", lobbyName, finalMessage.size());
        return;
    }

    SPDLOG_INFO("Current seqNum for {} is {}", lobbyName, sequenceNumber);
    itemEntry = finalMessage;
    sequenceNumber++;

    size_t expectedBencodeSize = std::to_string(message.size()).length() + 1 + message.size();

    std::vector<char> bencodedItem;
    bencodedItem.reserve(expectedBencodeSize);
    lt::bencode(std::back_inserter(bencodedItem), itemEntry);

    lt::dht::signature generatedSignature = lt::dht::sign_mutable_item(bencodedItem, itemSalt, lt::dht::sequence_number(sequenceNumber), lt::dht::public_key(publicKey.data()), lt::dht::secret_key(privateKey.data()));

    itemSignature = generatedSignature.bytes;
}

bool Dropper::dropMessage(const std::string &message, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore)
{
    if (!m_session)
    {
        SPDLOG_ERROR("Dropper::dropMessage failed: session is null");
        return false;
    }

    SPDLOG_INFO("Dropper is putting mutable item (message) into DHT");

    auto argon2Bytes = Hasher::getInstance().generateArgon2Salt(lobbyName);
    std::string salt(argon2Bytes.begin(), argon2Bytes.end());

    m_session->dht_put_item(m_publicKey, std::bind(&put_mutable_callback, message, lobbyName, lobbyStore, m_publicKey, m_privateKey, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4), salt);

    return true;
}
