#pragma once

struct ChatPublishParams
{
    std::string encryptedPayload;
    std::string lobbyName;
    int64_t targetSeqNum;
    std::string sequenceTargetKey;
    std::string messageTargetKey;
    std::string announcedTargetKey;
};
