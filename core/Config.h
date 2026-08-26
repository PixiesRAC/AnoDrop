#pragma once

#include <string>

class Config
{
  public:
    Config(int argc, char *argv[]);

    std::string getStorePath() const;
    std::string getLogPath() const;
    int getFetchInterval() const;
    int getArgon2Iterations() const;

  private:
    std::string m_storePath;
    std::string m_logPath;
    int m_fetchInterval{10000};
    int m_argon2Iterations{5};
};
