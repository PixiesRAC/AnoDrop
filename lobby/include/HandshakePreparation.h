#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
class LobbyStore;
// Persistence and key generation are deliberately outside the pure state engine.
class HandshakePreparation {
public:
    static std::optional<std::vector<unsigned char>> prepareLobbyPayload(
        LobbyStore& store, const std::string& lobby, const std::string& pseudo);
};
