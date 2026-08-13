#include "crypto/HandShakeChannel.h"

#include <cstring>
#include <libtorrent/kademlia/ed25519.hpp>
#include <stdexcept>
#include <tuple>

HandShakeChannel::HandShakeChannel(const std::vector<unsigned char>& seed) 
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
    
    m_publicKey = pk.bytes;
    m_privateKey = sk.bytes;
}

HandShakeChannel::~HandShakeChannel() {}

std::array<char, 32> HandShakeChannel::getPublicKey() const 
{
    return m_publicKey;
}

std::array<char, 64> HandShakeChannel::getPrivateKey() const 
{
    return m_privateKey;
}
