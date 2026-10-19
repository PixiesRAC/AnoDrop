#pragma once
#include "Hasher.h"
#include "HandshakeKeyService.h"
#include "CryptoUtils.h"
#include "SecureBuffer.h"

namespace network {
// Single wire-compatible address derivation policy. No salt/KDF changes.
struct DhtAddress {
    static std::string saltForLobby(const std::string& lobby) {
        auto salt = Hasher::getInstance().generateArgon2Salt(lobby);
        return {salt.begin(), salt.end()};
    }
    static HandshakeKeyService::ChannelKeys keys(const std::string& material, const std::string& lobby) {
        std::span<const unsigned char> materialSpan(reinterpret_cast<const unsigned char*>(material.data()), material.size());
        auto seed = CryptoUtils::deriveHKDF(materialSpan, {}, "DHTSeed", 32);
        if (!seed) throw std::runtime_error(seed.error());
        ScopedCleanse seedGuard(*seed);
        return HandshakeKeyService::ChannelKeys(*seed, saltForLobby(lobby));
    }
};
}
