#pragma once

#include <string>

class Logger
{
  public:
    static constexpr const char* DEFAULT_LOG_PATH = "anodrop.log";
    static constexpr const char* DEFAULT_LOG_LEVEL = "info";

    static void init(const std::string &logPath = DEFAULT_LOG_PATH, const std::string &logLevel = DEFAULT_LOG_LEVEL);
};
