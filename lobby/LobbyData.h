#pragma once

#include <nlohmann/json.hpp>
#include <string>

enum class LobbyStatus
{
    PENDING_HANDSHAKE = 0,
    HANDSHAKE_DONE = 1,
    ANOMALY = 2
};

struct LobbyData
{
    std::string lobby_name;
    std::string my_pseudo;
    std::string my_x25519_pub;
    std::string my_x25519_priv;
    std::string other_pseudo;
    std::string announced_x25519_pub;
    std::string private_node_discussion;
    LobbyStatus status;

    bool isEmpty() const
    {
        return my_x25519_pub.empty() && my_x25519_priv.empty();
    }
};

inline void to_json(nlohmann::json &j, const LobbyData &l)
{
    j = nlohmann::json{{"lobby_name", l.lobby_name}, {"my_pseudo", l.my_pseudo}, {"my_x25519_pub", l.my_x25519_pub}, {"my_x25519_priv", l.my_x25519_priv}, {"other_pseudo", l.other_pseudo}, {"announced_x25519_pub", l.announced_x25519_pub}, {"private_node_discussion", l.private_node_discussion}, {"status", l.status}};
}

inline void from_json(const nlohmann::json &j, LobbyData &l)
{
    l.lobby_name = j.value("lobby_name", "");
    l.my_pseudo = j.value("my_pseudo", "");
    l.my_x25519_pub = j.value("my_x25519_pub", "");
    l.my_x25519_priv = j.value("my_x25519_priv", "");
    l.other_pseudo = j.value("other_pseudo", "");
    l.announced_x25519_pub = j.value("announced_x25519_pub", "");
    l.private_node_discussion = j.value("private_node_discussion", "");
    l.status = j.value("status", LobbyStatus::PENDING_HANDSHAKE);
}

struct PeerInfo
{
    std::string pseudo;
    std::string x25519_pub;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PeerInfo, pseudo, x25519_pub)
