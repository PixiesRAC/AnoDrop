#pragma once

#include <string>

class Config
{
  public:
    Config(int argc, char *argv[]);

    [[nodiscard]] const std::string &getStorePath() const noexcept;
    [[nodiscard]] const std::string &getLogPath() const noexcept;
    [[nodiscard]] int getFetchInterval() const noexcept;
    [[nodiscard]] int getArgon2Iterations() const noexcept;
    [[nodiscard]] const std::string &getLogLevel() const noexcept;
    [[nodiscard]] int getMaxConcurrentFetches() const noexcept;
    [[nodiscard]] const std::string &getExtraSaltDataForLobbyHandshake() const noexcept;
    [[nodiscard]] bool getPullMessagesOnStartup() const noexcept;
    [[nodiscard]] const std::string &getNetworkInterface() const noexcept;

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
