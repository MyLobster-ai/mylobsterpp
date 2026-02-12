#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

namespace openclaw {

class Logger {
public:
    static void init(std::string_view name = "openclaw", std::string_view level = "info");
    static auto get() -> std::shared_ptr<spdlog::logger>&;

    static void set_level(std::string_view level);
    static void flush();
};

} // namespace openclaw

#define LOG_TRACE(...) SPDLOG_LOGGER_TRACE(::openclaw::Logger::get(), __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(::openclaw::Logger::get(), __VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_LOGGER_INFO(::openclaw::Logger::get(), __VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_LOGGER_WARN(::openclaw::Logger::get(), __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(::openclaw::Logger::get(), __VA_ARGS__)
#define LOG_FATAL(...) SPDLOG_LOGGER_CRITICAL(::openclaw::Logger::get(), __VA_ARGS__)
