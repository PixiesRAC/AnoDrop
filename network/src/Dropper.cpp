#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "Dropper.h"

#include <libtorrent/bencode.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/kademlia/item.hpp>
#include <spdlog/spdlog.h>

#include "DhtManager.h"
#include "HandshakePayloadCodec.h"
#include "HandshakeStateEngine.h"
#include "Hasher.h"

Dropper::Dropper(lt::session *session, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey) : m_session(session), m_publicKey(publicKey), m_privateKey(privateKey)
{
}

Dropper::~Dropper()
{
    SPDLOG_INFO("~Dropper: destroying Dropper...");
}

static void put_mutable_callback_handshake(const std::string &ourEncryptedPayload, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, std::string const &dhtSalt)
{
    auto ourDecryptedPayload = HandshakePayloadCodec::decryptDhtHandShakePayload(ourEncryptedPayload, lobbyName);
    if (!ourDecryptedPayload || !ourDecryptedPayload->is_array() || ourDecryptedPayload->empty())
    {
        SPDLOG_ERROR("Dropper callback: our own payload is invalid");
        return;
    }

    auto ourPeerInfo = (*ourDecryptedPayload)[0];

    [[maybe_unused]] bool shouldWrite = true;
    bool isHandshakeDone = false;
    std::string finalEncryptedPayload = ourEncryptedPayload;

    if (dhtSequenceNumber > 0 && dhtMutableItem.type() == lt::entry::string_t)
    {
        if (dhtSequenceNumber >= INT64_MAX - 2)
        {
            SPDLOG_INFO("Handshake is done for both parties of lobby {}", lobbyName);
            isHandshakeDone = true;
        }
        auto existingDecryptedPayload = HandshakePayloadCodec::decryptDhtHandShakePayload(dhtMutableItem.string(), lobbyName);

        if (!existingDecryptedPayload)
        {
            SPDLOG_WARN("Lobby {} payload is corrupted. Set to ANOMALY.", lobbyName);
            if (lobbyStore)
                lobbyStore->asyncUpdateLobbyData(lobbyName, {{"status", LobbyStatus::ANOMALY}});
            return;
        }
        if (lobbyStore)
        {
            nlohmann::json resolvedPatch = HandshakeStateEngine::resolvePayload(*existingDecryptedPayload, lobbyName, lobbyStore);
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

                if (resolvedPatch.contains("status") && resolvedPatch["status"].get<int>() == static_cast<int>(LobbyStatus::HANDSHAKE_DONE))
                {
                    SPDLOG_INFO("Handshake complete for lobby {}. Setting seqNum to "
                                "INT64_MAX - 1",
                                lobbyName);
                    dhtSequenceNumber = INT64_MAX - 2; // Will become INT64_MAX - 1 after the ++ below
                }
            }
        }

        if (!isHandshakeDone)
        {
            auto mergeRes = HandshakeStateEngine::mergeHandshakePayloads(*existingDecryptedPayload, ourPeerInfo, lobbyName);
            if (mergeRes)
            {
                finalEncryptedPayload = std::string(mergeRes->begin(), mergeRes->end());
            }
        }
        else
        {
            auto encRes = HandshakePayloadCodec::encryptDhtHandshakePayload(*existingDecryptedPayload, lobbyName);
            if (encRes)
            {
                finalEncryptedPayload = std::string(encRes->begin(), encRes->end());
            }
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
        return;
    }

    SPDLOG_INFO("Current seqNum for {} is {}", lobbyName, dhtSequenceNumber);
    dhtMutableItem = finalEncryptedPayload;
    dhtSequenceNumber++;

    size_t expectedBencodeSize = std::to_string(finalEncryptedPayload.size()).length() + 1 + finalEncryptedPayload.size();

    std::vector<char> bencodedItem;
    bencodedItem.reserve(expectedBencodeSize);
    lt::bencode(std::back_inserter(bencodedItem), dhtMutableItem);

    lt::dht::signature generatedSignature = lt::dht::sign_mutable_item(bencodedItem, dhtSalt, lt::dht::sequence_number(dhtSequenceNumber), lt::dht::public_key(publicKey.data()), lt::dht::secret_key(privateKey.data()));

    dhtSignature = generatedSignature.bytes;
}

static std::string getLobbySalt(const std::string &lobbyName)
{
    auto argon2Bytes = Hasher::getInstance().generateArgon2Salt(lobbyName);
    return std::string(argon2Bytes.begin(), argon2Bytes.end());
}

bool Dropper::dropHandShakeMessage(const std::string &message, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore, DhtManager *dhtManager)
{
    if (!m_session)
    {
        SPDLOG_ERROR("Dropper::dropHandShakeMessage failed: session is null");
        return false;
    }

    std::string salt = getLobbySalt(lobbyName);

    lt::hasher h(m_publicKey.data(), static_cast<int>(m_publicKey.size()));
    if (!salt.empty())
    {
        h.update(salt.data(), static_cast<int>(salt.size()));
    }
    std::string targetIDHex = Util::toHex(h.final().to_string());

    if (dhtManager) {
        dhtManager->registerPendingPut(targetIDHex, lobbyName, 0);
    }

    SPDLOG_INFO("Dropper is putting mutable item (handshake) into DHT for lobby {}. Target ID: {}", lobbyName, targetIDHex);

    // m_publicKey is the Ed25519 public key deterministically derived from the SHA-256 hash of lobbyName
    // This is where we send the message to the network
    m_session->dht_put_item(m_publicKey, std::bind(&put_mutable_callback_handshake, message, lobbyName, lobbyStore, m_publicKey, m_privateKey, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4), salt);

    return true;
}

static void put_mutable_callback_anomaly(const std::string &finalEncryptedPayload, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, std::string const &dhtSalt)
{
    dhtMutableItem = finalEncryptedPayload;
    dhtSequenceNumber = INT64_MAX; // Force MAX for anomaly

    size_t expectedBencodeSize = std::to_string(finalEncryptedPayload.size()).length() + 1 + finalEncryptedPayload.size();
    std::vector<char> bencodedItem;
    bencodedItem.reserve(expectedBencodeSize);
    lt::bencode(std::back_inserter(bencodedItem), dhtMutableItem);

    lt::dht::signature generatedSignature = lt::dht::sign_mutable_item(bencodedItem, dhtSalt, lt::dht::sequence_number(dhtSequenceNumber), lt::dht::public_key(publicKey.data()), lt::dht::secret_key(privateKey.data()));

    dhtSignature = generatedSignature.bytes;
}

static void put_mutable_callback_heartbeat(const std::string &timestamp, std::int64_t fakeTimestamp, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, std::string const &dhtSalt)
{
    dhtMutableItem = timestamp;
    if (fakeTimestamp > dhtSequenceNumber)
    {
        dhtSequenceNumber = fakeTimestamp;
    }
    else
    {
        dhtSequenceNumber++;
    }

    size_t expectedBencodeSize = std::to_string(timestamp.size()).length() + 1 + timestamp.size();
    std::vector<char> bencodedItem;
    bencodedItem.reserve(expectedBencodeSize);
    lt::bencode(std::back_inserter(bencodedItem), dhtMutableItem);

    lt::dht::signature generatedSignature = lt::dht::sign_mutable_item(bencodedItem, dhtSalt, lt::dht::sequence_number(dhtSequenceNumber), lt::dht::public_key(publicKey.data()), lt::dht::secret_key(privateKey.data()));

    dhtSignature = generatedSignature.bytes;
}

bool Dropper::dropAnomalyMessage(const std::string &message, const std::string &lobbyName)
{
    if (!m_session)
    {
        SPDLOG_ERROR("Dropper::dropAnomalyMessage failed: session is null");
        return false;
    }

    std::string salt = getLobbySalt(lobbyName);

    nlohmann::json payload;
    payload["anomaly"] = true;
    payload["message"] = message;

    auto encRes = HandshakePayloadCodec::encryptDhtHandshakePayload(payload, lobbyName);
    if (!encRes)
    {
        SPDLOG_ERROR("dropAnomalyMessage: encryption failed");
        return false;
    }
    std::string finalEncryptedPayload(encRes->begin(), encRes->end());

    SPDLOG_WARN("Dropper is putting ANOMALY into DHT for lobby {}", lobbyName);

    m_session->dht_put_item(m_publicKey, std::bind(&put_mutable_callback_anomaly, finalEncryptedPayload, m_publicKey, m_privateKey, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4), salt);

    return true;
}

bool Dropper::dropHeartbeatMessage(int offsetSeconds, const std::string &lobbyName)
{
    if (!m_session)
    {
        SPDLOG_ERROR("Dropper::dropAnomalyMessage failed: session is null");
        return false;
    }

    std::string salt = getLobbySalt(lobbyName);

    int64_t heartbeatTimestamp = TimeUtils::computeFakeTimestamp(offsetSeconds);

    SPDLOG_DEBUG("Dropper is putting HEARTBEAT ({}) into DHT for lobby {}", heartbeatTimestamp, lobbyName);

    m_session->dht_put_item(m_publicKey, std::bind(&put_mutable_callback_heartbeat, std::to_string(heartbeatTimestamp), heartbeatTimestamp, m_publicKey, m_privateKey, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4), salt);

    return true;
}

static void put_mutable_callback_ack(const std::string &dataToPut, std::int64_t fakeTimestamp, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, std::string const &dhtSalt)
{
    dhtMutableItem = dataToPut;
    if (fakeTimestamp > dhtSequenceNumber)
    {
        dhtSequenceNumber = fakeTimestamp;
    }
    else
    {
        dhtSequenceNumber++;
    }

    size_t expectedBencodeSize = std::to_string(dataToPut.size()).length() + 1 + dataToPut.size();
    std::vector<char> bencodedItem;
    bencodedItem.reserve(expectedBencodeSize);
    lt::bencode(std::back_inserter(bencodedItem), dhtMutableItem);
    lt::dht::signature generatedSignature = lt::dht::sign_mutable_item(bencodedItem, dhtSalt, lt::dht::sequence_number(dhtSequenceNumber), lt::dht::public_key(publicKey.data()), lt::dht::secret_key(privateKey.data()));
    dhtSignature = generatedSignature.bytes;
}

bool Dropper::dropAckMessage(const std::string &message, const std::string &lobbyName, int offsetSeconds)
{
    if (!m_session)
    {
        SPDLOG_ERROR("Dropper::dropAckMessage failed: session is null");
        return false;
    }

    std::string salt = getLobbySalt(lobbyName);
    int64_t fakeTimestamp = TimeUtils::computeFakeTimestamp(offsetSeconds);

    SPDLOG_DEBUG("Dropper is putting ACK into DHT for lobby {}", lobbyName);

    m_session->dht_put_item(m_publicKey, std::bind(&put_mutable_callback_ack, message, fakeTimestamp, m_publicKey, m_privateKey, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4), salt);

    return true;
}

// targetSeqNum is our authoritative sequence number. We use it directly as the DHT
// mutable item sequence number instead of incrementing dhtSequenceNumber from the DHT's
// current value. This prevents stale writes when sending many messages rapidly: if the
// DHT returns an outdated seqNum (e.g. 10) while we're actually at 125, incrementing
// would produce 11 and be rejected. Using our own value guarantees correctness.
static void put_mutable_callback_chat(const std::string &dataToPut, std::int64_t targetSeqNum, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, std::string const &dhtSalt)
{
    dhtMutableItem = dataToPut;
    dhtSequenceNumber = targetSeqNum;

    size_t expectedBencodeSize = std::to_string(dataToPut.size()).length() + 1 + dataToPut.size();
    std::vector<char> bencodedItem;
    bencodedItem.reserve(expectedBencodeSize);
    lt::bencode(std::back_inserter(bencodedItem), dhtMutableItem);

    lt::dht::signature generatedSignature = lt::dht::sign_mutable_item(bencodedItem, dhtSalt, lt::dht::sequence_number(dhtSequenceNumber), lt::dht::public_key(publicKey.data()), lt::dht::secret_key(privateKey.data()));

    dhtSignature = generatedSignature.bytes;
}

bool Dropper::dropChatMessage(const ChatPublishParams &params, bool isSeqNumMsg, DhtManager *dhtManager)
{
    if (!m_session)
    {
        SPDLOG_ERROR("Dropper::dropChatMessage failed: session is null");
        return false;
    }

    std::string salt = getLobbySalt(params.lobbyName);
    std::string dataToPut;

    lt::hasher h(m_publicKey.data(), static_cast<int>(m_publicKey.size()));
    if (!salt.empty())
    {
        h.update(salt.data(), static_cast<int>(salt.size()));
    }
    std::string targetIDHex = Util::toHex(h.final().to_string());

    std::string pubKeyHex = Util::toHex(std::string(m_publicKey.data(), m_publicKey.size()));
    SPDLOG_INFO("Dropper is putting mutable item into DHT. isSeqNum: {}, Target ID (SHA-1): {}, PubKey: {}", isSeqNumMsg, targetIDHex, pubKeyHex);

    if (isSeqNumMsg)
    {
        dataToPut = std::to_string(params.targetSeqNum);
    }
    else
    {
        dataToPut = params.encryptedPayload;
    }

    // Register pending put for delivery tracking (message payload only, not seqNum)
    if (!isSeqNumMsg && dhtManager)
    {
        dhtManager->registerPendingPut(targetIDHex, params.lobbyName, params.targetSeqNum);
    }

    m_session->dht_put_item(m_publicKey, std::bind(&put_mutable_callback_chat, dataToPut, params.targetSeqNum, m_publicKey, m_privateKey, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4), salt);

    return true;
}