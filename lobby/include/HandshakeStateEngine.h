#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class LobbyStore;

struct HandshakeTransitionResult
{
    bool shouldWrite{false};
    std::string payloadToWrite;
    std::int64_t targetSequenceNumber{0};
};

class HandshakeStateEngine
{
  public:
    [[nodiscard]] static std::optional<std::vector<unsigned char>> prepareLobbyPayload(std::shared_ptr<LobbyStore> lobbyStore, const std::string &lobbyName, const std::string &pseudo);
    [[nodiscard]] static nlohmann::json resolvePayload(const nlohmann::json &rawPayload, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore);
    [[nodiscard]] static std::expected<std::vector<unsigned char>, std::string> mergeHandshakePayloads(const nlohmann::json &oldDecrypted, const nlohmann::json &myPeerInfo, const std::string &lobbyName);
    [[nodiscard]] static nlohmann::json generateFakePeersArray();

    [[nodiscard]] static HandshakeTransitionResult processHandshakeTransition(
        const std::string &ourEncryptedPayload,
        const std::string &lobbyName,
        std::shared_ptr<LobbyStore> lobbyStore,
        std::int64_t currentDhtSeqNum,
        const std::string &existingItemString,
        bool hasExistingItem);
};
