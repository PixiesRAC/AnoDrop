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
    std::string getLogLevel() const;
    int getMaxConcurrentFetches() const;
    std::string getExtraSaltDataForLobbyHandshake() const;

  private:
    std::string m_storePath;
    std::string m_logPath;
    std::string m_logLevel{"info"};
    int m_fetchInterval{10000};
    int m_argon2Iterations{5};
    int m_maxConcurrentFetches{5};
    std::string m_extraSaltDataForLobbyHandshake{"AnoDrop"};
};
