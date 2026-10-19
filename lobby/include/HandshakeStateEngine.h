#pragma once
#include "LobbyUpdate.h"
#include <expected>
#include <string>

struct HandshakeTransitionResult {
    bool shouldWrite{false};
    std::string payloadToWrite;
    std::int64_t targetSequenceNumber{};
    LobbyUpdate update;
    bool established{false};
};

class HandshakeStateEngine {
public:
    // Pure transition: no storage, callbacks or shared state.
    [[nodiscard]] static std::expected<HandshakeTransitionResult, std::string> processHandshakeTransition(
        const std::string& ownPayload, const LobbyData& local,
        std::int64_t sequence, const std::string& existing, bool found);
};
