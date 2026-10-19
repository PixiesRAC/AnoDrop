#include "LogEvents.h"
#include "HandshakePreparation.h"

#include <spdlog/spdlog.h>

#include "CryptoUtils.h"
#include "DhtAddress.h"
#include "HandshakePayloadCodec.h"
#include "LobbyDataSerializer.h"
#include "LobbyStore.h"
#include "Util.h"

std::optional<std::vector<unsigned char>> HandshakePreparation::prepareLobbyPayload(LobbyStore &lobbyStore, const std::string &lobbyName, const std::string &pseudo)
{
    PeerInfo peerInfo;
    bool active = false;
    lobbyStore.transact(lobbyName, [&](const LobbyData &lobbyData)
                        {
        LobbyUpdate patch;
        if (lobbyData.metadata.is_hidden ||
            (lobbyData.metadata.status != LobbyStatus::PENDING_HANDSHAKE &&
             lobbyData.metadata.status != LobbyStatus::CONFIRMATION_PENDING)) return patch;
        active = true;
        const std::string existingPub = lobbyData.crypto ? lobbyData.crypto->my_x25519_pub : "";
        const SecureString existingPriv = lobbyData.crypto ? lobbyData.crypto->my_x25519_priv : SecureString{};
        const std::string announcedPub = lobbyData.crypto ? lobbyData.crypto->announced_x25519_pub : "";

        if (existingPub.empty() || existingPriv.empty())
        {
            auto keyPairRes = CryptoUtils::generateX25519KeyPair();
            if (!keyPairRes)
            {
                SPDLOG_ERROR("[HANDSHAKE] Failed to generate X25519 keypair for lobby {}: {}", LogEvents::lobbyId(lobbyName), keyPairRes.error());
                throw std::runtime_error(keyPairRes.error());
            }
            CryptoUtils::X25519KeyPair keyPair = std::move(*keyPairRes);
            ScopedCleanse privateGuard(keyPair.privateKey);
            patch.my_x25519_pub = Util::toHex(keyPair.publicKey);
            patch.my_x25519_priv = Util::toHex(keyPair.privateKey);
            OPENSSL_cleanse(keyPair.privateKey.data(), keyPair.privateKey.size());
            patch.status = LobbyStatus::PENDING_HANDSHAKE;
            patch.my_seq_num = 0;
            patch.announced_seq_num = 0;

            FakeTimezone tz = FakeTimezonePool::pickRandom();
            patch.fake_timezone = tz;
        }

        if (lobbyData.metadata.fake_timezone.name.empty() && !patch.fake_timezone)
            patch.fake_timezone = FakeTimezonePool::pickRandom();
        patch.my_pseudo = pseudo;

        peerInfo = PeerInfo{pseudo, patch.my_x25519_pub.value_or(existingPub),
            patch.fake_timezone.value_or(lobbyData.metadata.fake_timezone), announcedPub, 2};
        return patch; });
    if (!active)
        return std::nullopt;
    nlohmann::json payload = nlohmann::json::array({peerInfo});

    auto encryptedRes = HandshakePayloadCodec::encryptDhtHandshakePayload(payload, lobbyName);
    if (!encryptedRes)
    {
        SPDLOG_ERROR("[HANDSHAKE] prepareLobbyPayload: encryption failed: {}", encryptedRes.error());
        return std::nullopt;
    }
    SPDLOG_INFO("[HANDSHAKE] Handshake prepared lobby={} target_sha1={} local_x25519_public_key={} confirmed_peer={} protocol_version={} value_bytes={}",
                LogEvents::lobbyId(lobbyName), network::DhtAddress::keys(lobbyName, lobbyName).targetIDHex, peerInfo.x25519_pub, peerInfo.confirmed_peer, peerInfo.protocol_version, encryptedRes->size());
    return *encryptedRes;
}
