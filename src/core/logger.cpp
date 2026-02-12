#include "openclaw/core/logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace openclaw {

namespace {
    std::shared_ptr<spdlog::logger> g_logger;
}

void Logger::init(std::string_view name, std::string_view level) {
    g_logger = spdlog::stdout_color_mt(std::string(name));
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    set_level(level);
}

auto Logger::get() -> std::shared_ptr<spdlog::logger>& {
    if (!g_logger) {
        init();
    }
    return g_logger;
}

void Logger::set_level(std::string_view level) {
    if (level == "trace") g_logger->set_level(spdlog::level::trace);
    else if (level == "debug") g_logger->set_level(spdlog::level::debug);
    else if (level == "info") g_logger->set_level(spdlog::level::info);
    else if (level == "warn") g_logger->set_level(spdlog::level::warn);
    else if (level == "error") g_logger->set_level(spdlog::level::err);
    else if (level == "critical") g_logger->set_level(spdlog::level::critical);
    else g_logger->set_level(spdlog::level::info);
}

void Logger::flush() {
    if (g_logger) g_logger->flush();
}

} // namespace openclaw
