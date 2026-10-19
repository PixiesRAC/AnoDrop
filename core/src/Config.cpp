#include "Config.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

Config::Config(int argc, char *argv[]) : m_storePath("store"), m_logPath("anodrop.log")
{
    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];
        if (arg == "-c" || arg == "--config")
        {
            if (i + 1 >= argc)
            {
                throw std::invalid_argument("Missing configuration file path after " + std::string(arg));
            }
            std::string configPath = argv[++i];
            std::ifstream configFile(configPath);
            if (!configFile.is_open())
            {
                throw std::invalid_argument("Failed to open config file: " + configPath);
            }

            nlohmann::json configJson;
            configFile >> configJson;

            struct Candidate
            {
                std::string storePath;
                std::string logPath;
                std::string logLevel;
                int fetchInterval;
                int argon2Iterations;
                int maxConcurrentFetches;
                std::string extraSaltDataForLobbyHandshake;
                bool pullMessagesOnStartup;
                std::string networkInterface;
            } candidate{
                m_storePath,
                m_logPath,
                m_logLevel,
                m_fetchInterval,
                m_argon2Iterations,
                m_maxConcurrentFetches,
                m_extraSaltDataForLobbyHandshake,
                m_pullMessagesOnStartup,
                m_networkInterface
            };

            if (configJson.contains("store_path") && configJson["store_path"].is_string())
            {
                candidate.storePath = configJson["store_path"].get<std::string>();
            }
            else if (configJson.contains("store_path"))
            {
                SPDLOG_WARN("[BOOT] Config file does not contain a valid 'store_path' string. Using default.");
            }

            if (configJson.contains("log_path") && configJson["log_path"].is_string())
            {
                candidate.logPath = configJson["log_path"].get<std::string>();
            }
            else if (configJson.contains("log_path"))
            {
                SPDLOG_WARN("[BOOT] Config file does not contain a valid 'log_path' string. Using default.");
            }

            if (configJson.contains("log_level") && configJson["log_level"].is_string())
            {
                candidate.logLevel = configJson["log_level"].get<std::string>();
            }

            if (configJson.contains("fetch_interval_ms") && configJson["fetch_interval_ms"].is_number_integer())
            {
                candidate.fetchInterval = configJson["fetch_interval_ms"].get<int>();
            }

            if (configJson.contains("argon2_iterations") && configJson["argon2_iterations"].is_number_integer())
            {
                candidate.argon2Iterations = configJson["argon2_iterations"].get<int>();
            }

            if (configJson.contains("max_concurrent_fetches") && configJson["max_concurrent_fetches"].is_number_integer())
            {
                candidate.maxConcurrentFetches = configJson["max_concurrent_fetches"].get<int>();
            }

            if (configJson.contains("extra_salt_data_for_lobby_handshake") && configJson["extra_salt_data_for_lobby_handshake"].is_string())
            {
                candidate.extraSaltDataForLobbyHandshake = configJson["extra_salt_data_for_lobby_handshake"].get<std::string>();
            }

            if (configJson.contains("pull_messages_on_startup") && configJson["pull_messages_on_startup"].is_boolean())
            {
                candidate.pullMessagesOnStartup = configJson["pull_messages_on_startup"].get<bool>();
            }

            if (configJson.contains("network_interface") && configJson["network_interface"].is_string())
            {
                candidate.networkInterface = configJson["network_interface"].get<std::string>();
            }

            if (candidate.fetchInterval < 100 || candidate.fetchInterval > 3600000 ||
                candidate.argon2Iterations < 1 || candidate.argon2Iterations > 20 ||
                candidate.maxConcurrentFetches < 1 || candidate.maxConcurrentFetches > 64)
            {
                throw std::invalid_argument("Configuration out of range: fetch interval, Argon2 iterations or fetch limit");
            }

            m_storePath = std::move(candidate.storePath);
            m_logPath = std::move(candidate.logPath);
            m_logLevel = std::move(candidate.logLevel);
            m_fetchInterval = candidate.fetchInterval;
            m_argon2Iterations = candidate.argon2Iterations;
            m_maxConcurrentFetches = candidate.maxConcurrentFetches;
            m_extraSaltDataForLobbyHandshake = std::move(candidate.extraSaltDataForLobbyHandshake);
            m_pullMessagesOnStartup = candidate.pullMessagesOnStartup;
            m_networkInterface = std::move(candidate.networkInterface);
        }
    }
}

const std::string &Config::getStorePath() const noexcept
{
    return m_storePath;
}

const std::string &Config::getLogPath() const noexcept
{
    return m_logPath;
}

const std::string &Config::getLogLevel() const noexcept
{
    return m_logLevel;
}

int Config::getFetchInterval() const noexcept
{
    return m_fetchInterval;
}

int Config::getArgon2Iterations() const noexcept
{
    return m_argon2Iterations;
}

int Config::getMaxConcurrentFetches() const noexcept
{
    return m_maxConcurrentFetches;
}

const std::string &Config::getExtraSaltDataForLobbyHandshake() const noexcept
{
    return m_extraSaltDataForLobbyHandshake;
}

bool Config::getPullMessagesOnStartup() const noexcept
{
    return m_pullMessagesOnStartup;
}

const std::string &Config::getNetworkInterface() const noexcept
{
    return m_networkInterface;
}

