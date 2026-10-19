#pragma once

#include <string>
#include <cstdint>

struct ChatPublishParams
{
    std::string payload;
    std::string lobbyName;
    std::int64_t targetSeqNum{};
    std::string sequenceTargetKey;
    std::string messageTargetKey;
};
