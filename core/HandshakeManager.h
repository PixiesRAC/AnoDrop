#pragma once

#include <array>
#include <expected>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class LobbyStore;

class HandshakeManager
{
  public:
    struct ChannelKeys
    {
        ChannelKeys(const std::vector<unsigned char> &seed);

        std::array<char, 32> publicKey;
        std::array<char, 64> privateKey;
        std::string pubKeyHex;
        std::string targetIDHex;
    };

    static nlohmann::json resolvePayload(const nlohmann::json &rawPayload, const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore);

    static std::expected<std::string, std::string> deriveSharedSecret(const std::string &myPrivHex, const std::string &peerPubHex, const std::string &lobbyName);

    static std::string mergeHandshakePayloads(const nlohmann::json &oldDecrypted, const nlohmann::json &myPeerInfo, const std::string &lobbyName);

    static nlohmann::json generateFakePeersArray();
};
