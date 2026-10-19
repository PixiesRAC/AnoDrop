#include "Logger.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace {
class DebugSource final : public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg &message, const std::tm &, spdlog::memory_buf_t &dest) override {
        if (message.level <= spdlog::level::debug && !message.source.empty())
            fmt::format_to(std::back_inserter(dest), " [{}:{}] [{}]", message.source.filename, message.source.line, message.source.funcname);
    }
    std::unique_ptr<custom_flag_formatter> clone() const override { return std::make_unique<DebugSource>(); }
};
}

void Logger::init(const std::string &logPath, const std::string &logLevelStr)
{
    try
    {
        // Create with owner-only access before the sink opens or writes the file.
        const auto parent = std::filesystem::path(logPath).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        const int fd = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) throw std::runtime_error("Cannot create log file");
        const int modeResult = ::fchmod(fd, 0600);
        ::close(fd);
        if (modeResult != 0) throw std::runtime_error("Cannot restrict log file permissions");
        auto logger = spdlog::basic_logger_mt("file_logger", logPath);
        spdlog::set_default_logger(logger);

        auto formatter = std::make_unique<spdlog::pattern_formatter>(spdlog::pattern_time_type::utc);
        formatter->add_flag<DebugSource>('Q').set_pattern("%Y-%m-%dT%H:%M:%S.%eZ [%l] [thread=%t]%Q %v");
        spdlog::set_formatter(std::move(formatter));

        spdlog::level::level_enum lvl = spdlog::level::info;
        if (logLevelStr == "trace")
            lvl = spdlog::level::trace;
        else if (logLevelStr == "debug")
            lvl = spdlog::level::debug;
        else if (logLevelStr == "warn")
            lvl = spdlog::level::warn;
        else if (logLevelStr == "error")
            lvl = spdlog::level::err;

        spdlog::set_level(lvl);
        spdlog::flush_on(lvl);
        spdlog::flush_every(std::chrono::seconds(1));
        SPDLOG_INFO("[BOOT] Logger initialized. AnoDrop starting.");
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Log init failed: " << ex.what() << std::endl;
    }
}
