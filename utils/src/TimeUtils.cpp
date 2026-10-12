#include "TimeUtils.h"

#include <chrono>
#include <ctime>

std::int64_t TimeUtils::computeFakeTimestamp(int offset_seconds)
{
    return getCurrentEpochSeconds() + offset_seconds;
}

std::int64_t TimeUtils::getCurrentEpochSeconds()
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

TimestampValidationResult TimeUtils::validatePeerTimestamp(std::int64_t peerTimestamp,
                                                          std::int64_t currentFakeTime,
                                                          std::int64_t lastSeenTimestamp)
{
    if (peerTimestamp <= 0)
    {
        return TimestampValidationResult::Corrupted;
    }

    std::time_t ts = static_cast<std::time_t>(peerTimestamp);
    std::tm tm_info{};
    if (gmtime_r(&ts, &tm_info) == nullptr)
    {
        return TimestampValidationResult::Corrupted;
    }

    if (currentFakeTime > 0 && peerTimestamp > (currentFakeTime + CLOCK_SKEW_TOLERANCE_SECONDS))
    {
        return TimestampValidationResult::Future;
    }

    if (lastSeenTimestamp > 0 && peerTimestamp <= lastSeenTimestamp)
    {
        return TimestampValidationResult::NonMonotonic;
    }

    return TimestampValidationResult::Valid;
}
