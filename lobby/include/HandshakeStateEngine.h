#pragma once
#include <expected>
#include <nlohmann/json.hpp>

class LobbyStore;

class HandshakeStateEngine
{
  public:
    static std::optional<std::vector<unsigned char>> prepareLobbyPayload(std::shared_ptr<LobbyStore> lobbyStore, const std::string &lobbyName, const std::string &pseudo);
    static nlohmann::json resolvePayload(const nlohmann::json &rawPayload, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore);
    static std::expected<std::vector<unsigned char>, std::string> mergeHandshakePayloads(const nlohmann::json &oldDecrypted, const nlohmann::json &myPeerInfo, const std::string &lobbyName);
    static nlohmann::json generateFakePeersArray();
};
