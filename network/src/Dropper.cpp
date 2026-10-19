#include "LogEvents.h"
#include "Dropper.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "DhtMessageCodec.h"
#include "HandshakePayloadCodec.h"
#include "MutableItemCallbacks.h"
#include "TimeUtils.h"

using network::detail::put_mutable_callback_anomaly;
using network::detail::put_mutable_callback_handshake;
using network::detail::signAndPackMutableItem;

Dropper::Dropper(lt::session *session, const HandshakeKeyService::ChannelKeys &keys)
    : m_session(session),
      m_publicKey(keys.publicKey),
      m_privateKey(std::make_shared<SigningKey>(keys.privateKey)),
      m_salt(keys.salt),
      m_pubKeyHex(keys.pubKeyHex),
      m_targetIDHex(keys.targetIDHex)
{
}

Dropper::~Dropper()
{

}

bool Dropper::dropHandShakeMessage(const std::string &message, const std::string &lobbyName, std::int64_t sequence, DhtManager *dhtManager, DhtManager::PutCompletion completion)
{
    if (!m_session)
    {
        SPDLOG_ERROR("[DHT] Dropper::dropHandShakeMessage failed: session is null");
        return false;
    }

    if (!network::DhtMessageCodec::fitsBep44(message))
        return false;

    std::optional<std::uint64_t> reservation;
    if (dhtManager)
    {
        reservation = dhtManager->registerPendingPut(m_targetIDHex, lobbyName, sequence, std::move(completion));
        if (!reservation)
            return false;
    }
    try
    {
        SPDLOG_INFO("[DHT] Publish handshake | lobby={} target_sha1={} seq={}", nlohmann::json(lobbyName).dump(), m_targetIDHex, sequence);

        const auto pubKey = m_publicKey;
        const auto privKey = m_privateKey;
        m_session->dht_put_item(
            m_publicKey,
            [message, sequence, pubKey, privKey](lt::entry &item, std::array<char, 64> &sig, std::int64_t &seq, std::string const &salt)
            {
                put_mutable_callback_handshake(message, sequence, pubKey, privKey, item, sig, seq, salt);
            },
            m_salt);
    }
    catch (...)
    {
        if (reservation)
            dhtManager->cancelPendingPut(m_targetIDHex, *reservation);
        throw;
    }

    return true;
}

static void put_mutable_callback_timestamped(const std::string &dataToPut, std::int64_t fakeTimestamp, const std::array<char, 32> &publicKey, const std::shared_ptr<SigningKey> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, std::string const &dhtSalt, const std::string &logRole)
{
    if (dhtSequenceNumber == INT64_MAX)
        return;
    std::int64_t targetSeq = (fakeTimestamp > dhtSequenceNumber) ? fakeTimestamp : (dhtSequenceNumber + 1);
    signAndPackMutableItem(dataToPut, targetSeq, publicKey, privateKey, dhtMutableItem, dhtSignature, dhtSequenceNumber, dhtSalt, logRole);
}

bool Dropper::dropAnomalyMessage(const std::string &message, const std::string &lobbyName)
{
    if (!m_session)
    {
        SPDLOG_ERROR("[DHT] Dropper::dropAnomalyMessage failed: session is null");
        return false;
    }

    nlohmann::json payload;
    payload["anomaly"] = true;
    payload["message"] = message;

    auto encRes = HandshakePayloadCodec::encryptDhtHandshakePayload(payload, lobbyName);
    if (!encRes)
    {
        SPDLOG_ERROR("[DHT] dropAnomalyMessage: encryption failed");
        return false;
    }
    std::string finalEncryptedPayload(encRes->begin(), encRes->end());

    if (!network::DhtMessageCodec::fitsBep44(finalEncryptedPayload))
        return false;
    SPDLOG_INFO("[DHT] Publish anomaly | lobby={} target_sha1={}", nlohmann::json(lobbyName).dump(), m_targetIDHex);

    const auto pubKey = m_publicKey;
    const auto privKey = m_privateKey;
    m_session->dht_put_item(
        m_publicKey,
        [payloadStr = std::move(finalEncryptedPayload), pubKey, privKey](lt::entry &item, std::array<char, 64> &sig, std::int64_t &seq, std::string const &salt)
        {
            put_mutable_callback_anomaly(payloadStr, pubKey, privKey, item, sig, seq, salt);
        },
        m_salt);

    return true;
}

bool Dropper::dropHeartbeatMessage(int offsetSeconds, const std::string &lobbyName)
{
    if (!m_session)
    {
        SPDLOG_ERROR("[DHT] Dropper::dropHeartbeatMessage failed: session is null");
        return false;
    }

    int64_t heartbeatTimestamp = TimeUtils::computeFakeTimestamp(offsetSeconds);
    std::string dataToPut = std::to_string(heartbeatTimestamp);

    SPDLOG_INFO("[DHT] Publish heartbeat | lobby={} target_sha1={}", nlohmann::json(lobbyName).dump(), m_targetIDHex);

    const auto pubKey = m_publicKey;
    const auto privKey = m_privateKey;
    m_session->dht_put_item(
        m_publicKey,
        [data = std::move(dataToPut), heartbeatTimestamp, pubKey, privKey](lt::entry &item, std::array<char, 64> &sig, std::int64_t &seq, std::string const &salt)
        {
            put_mutable_callback_timestamped(data, heartbeatTimestamp, pubKey, privKey, item, sig, seq, salt, "heartbeat");
        },
        m_salt);

    return true;
}

bool Dropper::dropAckMessage(const std::string &message, const std::string &lobbyName, int offsetSeconds)
{
    if (!m_session)
    {
        SPDLOG_ERROR("[DHT] Dropper::dropAckMessage failed: session is null");
        return false;
    }

    if (!network::DhtMessageCodec::fitsBep44(message))
        return false;
    int64_t fakeTimestamp = TimeUtils::computeFakeTimestamp(offsetSeconds);

    SPDLOG_INFO("[DHT] Publish ack | lobby={} target_sha1={}", nlohmann::json(lobbyName).dump(), m_targetIDHex);

    const auto pubKey = m_publicKey;
    const auto privKey = m_privateKey;
    m_session->dht_put_item(
        m_publicKey,
        [message, fakeTimestamp, pubKey, privKey](lt::entry &item, std::array<char, 64> &sig, std::int64_t &seq, std::string const &salt)
        {
            put_mutable_callback_timestamped(message, fakeTimestamp, pubKey, privKey, item, sig, seq, salt, "ack");
        },
        m_salt);

    return true;
}

// targetSeqNum is our authoritative sequence number. We use it directly as the DHT
// mutable item sequence number instead of incrementing dhtSequenceNumber from the DHT's
// current value. This prevents stale writes when sending many messages rapidly: if the
// DHT returns an outdated seqNum (e.g. 10) while we're actually at 125, incrementing
// would produce 11 and be rejected. Using our own value guarantees correctness.
static void put_mutable_callback_chat(const std::string &dataToPut, std::int64_t targetSeqNum, const std::array<char, 32> &publicKey, const std::shared_ptr<SigningKey> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, std::string const &dhtSalt, const std::string &logRole)
{
    if (dhtSequenceNumber > targetSeqNum)
        return;
    signAndPackMutableItem(dataToPut, targetSeqNum, publicKey, privateKey, dhtMutableItem, dhtSignature, dhtSequenceNumber, dhtSalt, logRole);
}

bool Dropper::dropChatMessage(const ChatPublishParams &params, DhtManager *dhtManager, DhtManager::PutCompletion completion)
{
    if (!m_session)
    {
        SPDLOG_ERROR("[DHT] Dropper::dropChatMessage failed: session is null");
        return false;
    }

    if (!network::DhtMessageCodec::fitsBep44(params.payload))
        return false;

    SPDLOG_INFO("[DHT] Publish message | lobby={} target_sha1={} seq={}", nlohmann::json(params.lobbyName).dump(), m_targetIDHex, params.targetSeqNum);

    std::optional<std::uint64_t> reservation;
    if (dhtManager)
    {
        reservation = dhtManager->registerPendingPut(m_targetIDHex, params.lobbyName, params.targetSeqNum, std::move(completion));
        if (!reservation)
            return false;
    }
    try
    {
        const auto pubKey = m_publicKey;
        const auto privKey = m_privateKey;
        const auto payload = params.payload;
        const auto targetSeq = params.targetSeqNum;
        m_session->dht_put_item(
            m_publicKey,
            [payload, targetSeq, pubKey, privKey](lt::entry &item, std::array<char, 64> &sig, std::int64_t &seq, std::string const &salt)
            {
                put_mutable_callback_chat(payload, targetSeq, pubKey, privKey, item, sig, seq, salt, "message");
            },
            m_salt);
    }
    catch (...)
    {
        if (reservation)
            dhtManager->cancelPendingPut(m_targetIDHex, *reservation);
        throw;
    }

    return true;
}

bool Dropper::dropSequenceAnnouncement(const ChatPublishParams &params)
{
    if (!m_session)
    {
        SPDLOG_ERROR("[DHT] Dropper::dropSequenceAnnouncement failed: session is null");
        return false;
    }

    std::string dataToPut = std::to_string(params.targetSeqNum);
    if (!network::DhtMessageCodec::fitsBep44(dataToPut))
        return false;

    SPDLOG_INFO("[DHT] Publish sequence_announcement | lobby={} target_sha1={} seq={}", nlohmann::json(params.lobbyName).dump(), m_targetIDHex, params.targetSeqNum);

    const auto pubKey = m_publicKey;
    const auto privKey = m_privateKey;
    const auto targetSeq = params.targetSeqNum;
    m_session->dht_put_item(
        m_publicKey,
        [data = std::move(dataToPut), targetSeq, pubKey, privKey](lt::entry &item, std::array<char, 64> &sig, std::int64_t &seq, std::string const &salt)
        {
            put_mutable_callback_chat(data, targetSeq, pubKey, privKey, item, sig, seq, salt, "sequence_announcement");
        },
        m_salt);

    return true;
}
