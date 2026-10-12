#pragma once

#include <string>

class Config
{
  public:
    Config(int argc, char *argv[]);

    [[nodiscard]] std::string getStorePath() const;
    [[nodiscard]] std::string getLogPath() const;
    [[nodiscard]] int getFetchInterval() const;
    [[nodiscard]] int getArgon2Iterations() const;
    [[nodiscard]] std::string getLogLevel() const;
    [[nodiscard]] int getMaxConcurrentFetches() const;
    [[nodiscard]] std::string getExtraSaltDataForLobbyHandshake() const;
    [[nodiscard]] bool getPullMessagesOnStartup() const;
    [[nodiscard]] std::string getNetworkInterface() const;

  private:
    std::string m_storePath;
    std::string m_logPath;
    std::string m_logLevel{"info"};
    int m_fetchInterval{10000};
    int m_argon2Iterations{5};
    int m_maxConcurrentFetches{5};
    std::string m_extraSaltDataForLobbyHandshake{"AnoDrop"};
    bool m_pullMessagesOnStartup{true};
    std::string m_networkInterface{"any"};
};
