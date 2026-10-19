#include "LogEvents.h"
#include "DhtMessageCodec.h"
#include <algorithm>
#include "LobbyUpdate.h"
#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "HandshakeStateEngine.h"

#include <spdlog/spdlog.h>

#include "CryptoUtils.h"
#include "HandshakeKeyService.h"
#include "HandshakePayloadCodec.h"
#include "Util.h"

static HandshakeTransitionResult transition(
    const std::string& ownPayload, const LobbyData& local,
    std::int64_t sequence, const std::string& existing, bool found)
{
    HandshakeTransitionResult result;
    const auto& lobby = local.metadata.lobby_name;
    auto own = HandshakePayloadCodec::decryptDhtHandShakePayload(ownPayload, lobby);
    if (!own || !own->is_array() || own->size() != 1)
        throw std::runtime_error("Invalid local handshake payload");
    PeerInfo me = own->at(0).get<PeerInfo>();
    std::vector<PeerInfo> peers;
    std::optional<PeerInfo> existingSelf;
    if (found) {
        auto decoded = HandshakePayloadCodec::decryptDhtHandShakePayload(existing, lobby);
        if (!decoded) throw std::runtime_error("Invalid handshake authentication");
        if (decoded->is_object() && decoded->value("anomaly", false)) {
            result.update.status = LobbyStatus::ANOMALY;
            result.update.anomaly_message = decoded->value("message", "Peer anomaly");
            return result;
        }
        if (!decoded->is_array() || decoded->size() > 2)
            throw std::runtime_error("Invalid handshake participant count");
        for (const auto& value : *decoded) {
            auto peer = value.get<PeerInfo>();
            if (peer.protocol_version != 2) throw std::runtime_error("Peer requires handshake protocol v2");
            if (peer.x25519_pub.size() != 64 || Util::fromHex(peer.x25519_pub).size() != 32)
                throw std::runtime_error("Invalid handshake public key");
            if (peer.fake_timezone.offset_seconds < -86400 || peer.fake_timezone.offset_seconds > 86400)
                throw std::runtime_error("Invalid handshake timezone");
            if (peer.x25519_pub != me.x25519_pub) peers.push_back(std::move(peer));
            else existingSelf = std::move(peer);
        }
        if (peers.size() > 1) throw std::runtime_error("Ambiguous handshake peer");
    }
    if (!peers.empty()) {
        const auto& peer = peers.front();
        if (!local.crypto) throw std::runtime_error("Local lobby crypto state is missing");
        auto secret = HandshakeKeyService::deriveSharedSecret(local.crypto->my_x25519_priv, peer.x25519_pub, lobby);
        if (!secret) throw std::runtime_error(secret.error());
        if (!local.crypto->announced_x25519_pub.empty() && local.crypto->announced_x25519_pub != peer.x25519_pub)
            throw std::runtime_error("Handshake peer changed during confirmation");
        SPDLOG_INFO("[HANDSHAKE] Handshake peer validated local_x25519_public_key={} peer_x25519_public_key={} peer_confirmed_key={} protocol_version={} received_seq={}",
                    me.x25519_pub, peer.x25519_pub, peer.confirmed_peer, peer.protocol_version, sequence);
        me.confirmed_peer = peer.x25519_pub;
        result.update.other_pseudo = peer.pseudo;
        result.update.announced_x25519_pub = peer.x25519_pub;
        result.update.private_node_discussion = std::move(*secret);
        me.fake_timezone = me.x25519_pub < peer.x25519_pub ? me.fake_timezone : peer.fake_timezone;
        result.update.fake_timezone = me.fake_timezone;
        result.update.status = LobbyStatus::CONFIRMATION_PENDING;
        result.established = peer.confirmed_peer == me.x25519_pub;
    }
    if (found && sequence >= INT64_MAX - 2) {
        // A finalized transcript must contain both participants' confirmations.
        const bool ourConfirmationPresent =
            existingSelf && existingSelf->confirmed_peer == me.confirmed_peer && !me.confirmed_peer.empty();
        result.established = result.established && ourConfirmationPresent;
        if (result.established) result.update.status = LobbyStatus::HANDSHAKE_DONE;
        SPDLOG_INFO("[HANDSHAKE] Handshake finalized transcript received_seq={} established={} our_confirmation_present={}",
                    sequence, result.established, ourConfirmationPresent);
        return result;
    }
    peers.push_back(std::move(me));
    std::ranges::sort(peers, {}, &PeerInfo::x25519_pub);
    const nlohmann::json merged = peers;
    auto encoded = HandshakePayloadCodec::encryptDhtHandshakePayload(merged, lobby);
    if (!encoded) throw std::runtime_error(encoded.error());
    result.payloadToWrite.assign(encoded->begin(), encoded->end());
    if (!network::DhtMessageCodec::fitsBep44(result.payloadToWrite))
        throw std::runtime_error("Handshake exceeds BEP44 payload limit");
    result.targetSequenceNumber = result.established ? INT64_MAX - 1 : sequence + 1;
    result.shouldWrite = true;
    SPDLOG_INFO("[HANDSHAKE] Handshake transition received_seq={} found={} participants={} target_seq={} established={} should_write={}",
                sequence, found, peers.size(), result.targetSequenceNumber, result.established, result.shouldWrite);
    return result;
}

std::expected<HandshakeTransitionResult, std::string> HandshakeStateEngine::processHandshakeTransition(
    const std::string& ownPayload, const LobbyData& local,
    std::int64_t sequence, const std::string& existing, bool found)
{
    try {
        return transition(ownPayload, local, sequence, existing, found);
    } catch (const nlohmann::json::exception& error) {
        SPDLOG_WARN("[HANDSHAKE] lobby={} reason=json_validation | Transition rejected", LogEvents::lobbyId(local.metadata.lobby_name));
        return std::unexpected(std::string("Invalid handshake data: ") + error.what());
    } catch (const std::runtime_error& error) {
        SPDLOG_WARN("[HANDSHAKE] lobby={} reason=state_or_authentication | Transition rejected", LogEvents::lobbyId(local.metadata.lobby_name));
        return std::unexpected(std::string(error.what()));
    } catch (const std::invalid_argument& error) {
        SPDLOG_WARN("[HANDSHAKE] lobby={} reason=invalid_argument | Transition rejected", LogEvents::lobbyId(local.metadata.lobby_name));
        return std::unexpected(std::string(error.what()));
    } catch (const std::exception& error) {
        SPDLOG_WARN("[HANDSHAKE] lobby={} reason=processing_failure | Transition rejected", LogEvents::lobbyId(local.metadata.lobby_name));
        return std::unexpected(std::string("Handshake processing failed: ") + error.what());
    }
}
