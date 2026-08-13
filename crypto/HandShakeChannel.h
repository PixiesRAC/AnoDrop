#pragma once

#include <array>
#include <string>
#include <vector>

class HandShakeChannel
{
public:
    HandShakeChannel(const std::vector<unsigned char>& seed);
    ~HandShakeChannel();

    std::array<char, 32> getPublicKey() const;
    std::array<char, 64> getPrivateKey() const;

private:
    std::array<char, 32> m_publicKey;
    std::array<char, 64> m_privateKey;
};