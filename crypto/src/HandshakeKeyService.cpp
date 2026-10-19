#include "LogEvents.h"
#include "HandshakeKeyService.h"

#include <cstring>
#include <libtorrent/hasher.hpp>
#include <libtorrent/kademlia/ed25519.hpp>
#include <openssl/crypto.h>
#include <spdlog/spdlog.h>

#include "CryptoUtils.h"
#include "SecureBuffer.h"
#include "Util.h"

HandshakeKeyService::ChannelKeys::ChannelKeys(std::span<const unsigned char> seed, std::string saltValue)
    : salt(std::move(saltValue))
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
    OPENSSL_cleanse(sk.bytes.data(), sk.bytes.size());
    OPENSSL_cleanse(lt_seed.data(), lt_seed.size());

    pubKeyHex = Util::toHex(publicKey);

    lt::hasher h(publicKey.data(), static_cast<int>(publicKey.size()));
    if (!salt.empty())
    {
        h.update(salt.data(), static_cast<int>(salt.size()));
    }
    const lt::sha1_hash targetID = h.final();
    targetIDHex = Util::toHex(std::span<const char>(targetID.data(), targetID.size()));
}

std::expected<std::string, std::string> HandshakeKeyService::deriveSharedSecret(const std::string &myPrivHex, const std::string &peerPubHex, const std::string &lobbyName)
{
    try
    {
        if (!myPrivHex.empty() && !peerPubHex.empty())
        {
            std::vector<unsigned char> myPrivRaw = Util::fromHex(myPrivHex);
            ScopedCleanse privateGuard(myPrivRaw);
            std::vector<unsigned char> peerPubRaw = Util::fromHex(peerPubHex);
            ScopedCleanse peerGuard(peerPubRaw);
            auto sharedSecretRes = CryptoUtils::deriveX25519SharedSecret(myPrivRaw, peerPubRaw);
            if (!sharedSecretRes)
            {
                SPDLOG_WARN("[HANDSHAKE] Failed to derive X25519 shared secret for lobby {}: {}", LogEvents::lobbyId(lobbyName), sharedSecretRes.error());
                return std::unexpected(sharedSecretRes.error());
            }
            std::vector<unsigned char> sharedSecret = std::move(*sharedSecretRes);
            ScopedCleanse sharedGuard(sharedSecret);
            std::string hexSecret = Util::toHex(sharedSecret);
            SPDLOG_INFO("[HANDSHAKE] Successfully derived X25519 shared secret for lobby {}", LogEvents::lobbyId(lobbyName));
            return hexSecret;
        }
        else
        {
            SPDLOG_WARN("[HANDSHAKE] Cannot derive shared secret: missing keys for lobby {}", LogEvents::lobbyId(lobbyName));
            return std::unexpected("Missing keys for lobby " + lobbyName);
        }
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("[HANDSHAKE] Failed to derive shared secret for lobby {}: {}", LogEvents::lobbyId(lobbyName), e.what());
        return std::unexpected(std::string("Exception deriving secret: ") + e.what());
    }
}
