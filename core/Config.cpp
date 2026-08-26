#include "core/Config.h"

#include <fstream>
#include <nlohmann/json.hpp>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

Config::Config(int argc, char *argv[]) : m_storePath("store"), m_logPath("anodrop.log")
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc)
        {
            std::string configPath = argv[++i];
            try
            {
                std::ifstream configFile(configPath);
                if (configFile.is_open())
                {
                    nlohmann::json configJson;
                    configFile >> configJson;
                    if (configJson.contains("store_path") && configJson["store_path"].is_string())
                    {
                        m_storePath = configJson["store_path"].get<std::string>();
                    }
                    else
                    {
                        SPDLOG_WARN("Config file does not contain a valid 'store_path' "
                                    "string. Using default.");
                    }
                    if (configJson.contains("log_path") && configJson["log_path"].is_string())
                    {
                        m_logPath = configJson["log_path"].get<std::string>();
                    }
                    else
                    {
                        SPDLOG_WARN("Config file does not contain a valid 'log_path' "
                                    "string. Using default.");
                    }
                    if (configJson.contains("fetch_interval_ms") && configJson["fetch_interval_ms"].is_number_integer())
                    {
                        m_fetchInterval = configJson["fetch_interval_ms"].get<int>();
                    }
                    if (configJson.contains("argon2_iterations") && configJson["argon2_iterations"].is_number_integer())
                    {
                        m_argon2Iterations = configJson["argon2_iterations"].get<int>();
                    }
                }
                else
                {
                    SPDLOG_ERROR("Failed to open config file: {}", configPath);
                }
            }
            catch (const std::exception &e)
            {
                SPDLOG_ERROR("Error parsing config file: {}", e.what());
            }
        }
    }
}

std::string Config::getStorePath() const
{
    return m_storePath;
}

std::string Config::getLogPath() const
{
    return m_logPath;
}

int Config::getFetchInterval() const
{
    return m_fetchInterval;
}

int Config::getArgon2Iterations() const
{
    return m_argon2Iterations;
}
