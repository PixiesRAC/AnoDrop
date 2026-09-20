#pragma once

#include "lobby/LobbyStore.h"
#include <cstdint>
#include <memory>
#include <string>

struct ChatPublishParams
{
    std::string encryptedPayload;
    std::string lobbyName;
    int64_t targetSeqNum;
    std::string sequenceTargetKey;
    std::string messageTargetKey;
    std::string announcedTargetKey;
};
