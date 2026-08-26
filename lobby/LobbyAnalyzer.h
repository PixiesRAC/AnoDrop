#pragma once

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

class LobbyAnalyzer
{
  public:
    LobbyAnalyzer();
    ~LobbyAnalyzer() = default;

    bool isSequenceNumberCorrect(std::int64_t &sequenceNumber)
    {
        if (sequenceNumber > 3)
        {
            SPDLOG_ERROR("SeqNumber is INVALID / Lobby is corrupted / Please use a new one");
        }
    }
};