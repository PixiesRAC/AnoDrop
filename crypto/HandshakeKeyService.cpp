#include "crypto/HandshakeKeyService.h"
#include "crypto/CryptoUtils.h"
#include "utils/Util.h"
#include <libtorrent/hasher.hpp>
#include <libtorrent/kademlia/ed25519.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <tuple>

HandshakeKeyService::ChannelKeys::ChannelKeys(const std::vector<unsigned char> &seed, const std::string &salt)
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

    lt::hasher h(publicKey.data(), static_cast<int>(publicKey.size()));
    if (!salt.empty())
    {
        h.update(salt.data(), static_cast<int>(salt.size()));
    }
    lt::sha1_hash targetID = h.final();
    targetIDHex = Util::toHex(targetID.to_string());
}

std::expected<std::string, std::string> HandshakeKeyService::deriveSharedSecret(const std::string &myPrivHex, const std::string &peerPubHex, const std::string &lobbyName)
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
