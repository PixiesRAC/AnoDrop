#include "utils/Logger.h"

#include <iostream>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

void Logger::init(const std::string &logPath)
{
    try
    {
        auto logger = spdlog::basic_logger_mt("file_logger", logPath);
        spdlog::set_default_logger(logger);

        // Extract the base filename of the log file (e.g., "anodrop.log")
        std::string base_filename = logPath.substr(logPath.find_last_of("/\\") + 1);

        // Use ANSI escape codes directly for colors in the file sink
        // Cyan date, Yellow log file name, Magenta file/line and function, Green
        // level
        std::string pattern = "\033[36m[%Y-%m-%d %H:%M:%S.%e]\033[0m \033[33m[" + base_filename + "]\033[0m \033[35m[%s:%#] [%!]\033[0m \033[32m[%l]\033[0m %v";
        spdlog::set_pattern(pattern);

        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
        SPDLOG_INFO("Logger initialized. AnoDrop starting.");
    }
    catch (const spdlog::spdlog_ex &ex)
    {
        std::cerr << "Log init failed: " << ex.what() << std::endl;
    }
}
