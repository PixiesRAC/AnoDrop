#pragma once

#include <array>
#include <libtorrent/session.hpp>
#include <string>

class Dropper
{
public:
    // Initializes the Dropper with the target session and public key target ID
    Dropper(lt::session* session, const std::array<char, 32>& publicKey, const std::array<char, 64>& privateKey);
    ~Dropper();

    // Drops a message into the DHT network via BEP 44 mutable item
    bool dropMessage(const std::string& message);

private:
    lt::session* m_session;
    std::array<char, 32> m_publicKey;
    std::array<char, 64> m_privateKey;
};