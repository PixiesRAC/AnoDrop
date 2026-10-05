#pragma once

#include <cstdint>

class TimeUtils
{
  public:
    static std::int64_t computeFakeTimestamp(int offset_seconds);
    static std::int64_t getCurrentEpochSeconds();
};
