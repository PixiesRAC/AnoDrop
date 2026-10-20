#pragma once

#include <string>
#include <cstdint>

struct ChatPublishParams
{
    std::string payload; // Plaintext ChatMessage JSON; sealed only at PUT time by OutboxPublisher.
    std::string lobbyName;
    std::int64_t targetSeqNum{};
    std::string sequenceTargetKey;
    std::string messageTargetKey;
    std::string recipientX25519Pub; // Peer's X25519 public key (hex) used to seal the DHT payload.
};
