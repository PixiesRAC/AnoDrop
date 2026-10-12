#include "Dropper.h"

#include <libtorrent/bencode.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/kademlia/item.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Bep44Signer.h"
#include "DhtManager.h"
#include "HandshakePayloadCodec.h"
#include "HandshakeStateEngine.h"
#include "Hasher.h"
#include "LobbyDataSerializer.h"

Dropper::Dropper(lt::session *session, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey) : m_session(session), m_publicKey(publicKey), m_privateKey(privateKey)
{
}

Dropper::~Dropper()
{
    SPDLOG_INFO("~Dropper: destroying Dropper...");
}

static void signAndPackMutableItem(const std::string &payload, std::int64_t seqNum, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, const std::string &dhtSalt)
{
    network::Bep44Signer::signAndPackMutableItem(payload, seqNum, publicKey, privateKey, dhtMutableItem, dhtSignature, dhtSequenceNumber, dhtSalt);
}

static void put_mutable_callback_handshake(const std::string &ourEncryptedPayload, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, std::string const &dhtSalt)
{
    bool hasExisting = (dhtSequenceNumber > 0 && dhtMutableItem.type() == lt::entry::string_t);
    std::string existingItemStr = hasExisting ? dhtMutableItem.string() : "";

    auto transition = HandshakeStateEngine::processHandshakeTransition(
        ourEncryptedPayload, lobbyName, lobbyStore, dhtSequenceNumber, existingItemStr, hasExisting);

    if (!transition.shouldWrite)
    {
        return;
    }

    dhtSequenceNumber = transition.targetSequenceNumber - 1;
    SPDLOG_INFO("Current seqNum for {} is {}", lobbyName, dhtSequenceNumber);
    network::Bep44Signer::signAndPackMutableItem(
        transition.payloadToWrite, transition.targetSequenceNumber, publicKey, privateKey,
        dhtMutableItem, dhtSignature, dhtSequenceNumber, dhtSalt);
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

    if (dhtManager)
    {
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
    signAndPackMutableItem(finalEncryptedPayload, INT64_MAX, publicKey, privateKey, dhtMutableItem, dhtSignature, dhtSequenceNumber, dhtSalt); // Force MAX for anomaly
}

static void put_mutable_callback_heartbeat(const std::string &timestamp, std::int64_t fakeTimestamp, const std::array<char, 32> &publicKey, const std::array<char, 64> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, std::string const &dhtSalt)
{
    std::int64_t targetSeq = (fakeTimestamp > dhtSequenceNumber) ? fakeTimestamp : (dhtSequenceNumber + 1);
    signAndPackMutableItem(timestamp, targetSeq, publicKey, privateKey, dhtMutableItem, dhtSignature, dhtSequenceNumber, dhtSalt);
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
    std::int64_t targetSeq = (fakeTimestamp > dhtSequenceNumber) ? fakeTimestamp : (dhtSequenceNumber + 1);
    signAndPackMutableItem(dataToPut, targetSeq, publicKey, privateKey, dhtMutableItem, dhtSignature, dhtSequenceNumber, dhtSalt);
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
    signAndPackMutableItem(dataToPut, targetSeqNum, publicKey, privateKey, dhtMutableItem, dhtSignature, dhtSequenceNumber, dhtSalt);
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
