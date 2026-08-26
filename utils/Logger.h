#pragma once

#include <string>

class Logger
{
  public:
    static void init(const std::string &logPath = "anodrop.log");
};
