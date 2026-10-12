#pragma once

#include <cstdint>

enum class TimestampValidationResult
{
    Valid,
    Corrupted,    // <= 0 or invalid time format
    Future,       // Greater than current simulated/fake time
    NonMonotonic  // Less than or equal to last seen timestamp
};

class TimeUtils
{
  public:
    static constexpr std::int64_t CLOCK_SKEW_TOLERANCE_SECONDS = 300; // 5 minutes (300s) tolerance

    [[nodiscard]] static std::int64_t computeFakeTimestamp(int offset_seconds);
    [[nodiscard]] static std::int64_t getCurrentEpochSeconds();
    [[nodiscard]] static TimestampValidationResult validatePeerTimestamp(std::int64_t peerTimestamp,
                                                          std::int64_t currentFakeTime = 0,
                                                          std::int64_t lastSeenTimestamp = 0);
};
