#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

enum class DhtFetchStatus {
    Found,     // Item was found on DHT and validated/decrypted successfully.
    Missing,   // DHT nodes replied that the key/item does not exist.
    Timeout,   // Operation timed out without reaching authoritative consensus.
    Cancelled  // Operation was cancelled (lobby closed, quarantined, or shutting down).
};

enum class DhtFetchKind {
    Sequence,  // Peer's announced monotonic sequence channel.
    Message,   // Individual chat message payload.
    Signaling, // ACK or Heartbeat signaling channels.
    Handshake  // Initial 3-way handshake negotiation payload.
};
struct DhtFetchResult {
    DhtFetchStatus status{DhtFetchStatus::Missing};
    std::string value;
    std::int64_t sequence{};
    // Diagnostic context for callbacks sharing one DHT lookup.
    std::uint64_t requestId{};
    std::size_t callbackIndex{};
    std::size_t callbackCount{};
    [[nodiscard]] bool found() const { return status == DhtFetchStatus::Found; }
};
struct DhtRequest {
    DhtFetchKind kind{DhtFetchKind::Signaling};
    std::int64_t sequence{-1};
    std::string logRole{}; // Diagnostic context only; does not affect scheduling.
};
