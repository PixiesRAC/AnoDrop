#include "TimeUtils.h"

#include <chrono>

std::int64_t TimeUtils::computeFakeTimestamp(int offset_seconds)
{
    return getCurrentEpochSeconds() + offset_seconds;
}

std::int64_t TimeUtils::getCurrentEpochSeconds()
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}
