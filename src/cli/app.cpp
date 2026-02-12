#include "openclaw/cli/app.hpp"
#include "openclaw/cli/commands.hpp"
#include "openclaw/core/logger.hpp"

#include <filesystem>
#include <iostream>

// Version string; typically injected by CMake via -DOPENCLAW_VERSION_STRING=...
#ifndef OPENCLAW_VERSION_STRING
#define OPENCLAW_VERSION_STRING "0.1.0-dev"
#endif

namespace openclaw::cli {

App::App()
    : cli_("openclaw", "OpenClaw AI Assistant Platform")
{
    cli_.set_version_flag("--version", OPENCLAW_VERSION_STRING,
                          "Display version information");

    // Global option: config file path.
    cli_.add_option("-c,--config", config_path_,
                    "Path to configuration file (JSON)")
        ->envname("OPENCLAW_CONFIG")
        ->check(CLI::ExistingFile);

    // Global option: log level override.
    std::string log_level;
    cli_.add_option("--log-level", config_.log_level,
                    "Log level (trace, debug, info, warn, error, critical)")
        ->envname("OPENCLAW_LOG_LEVEL")
        ->default_val("info");

    // Require a subcommand.
    cli_.require_subcommand(1);

    setup_commands();
}

App::~App() = default;

auto App::run(int argc, char** argv) -> int {
    try {
        cli_.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return cli_.exit(e);
    }

    // Initialize the logger with the chosen level.
    Logger::init("openclaw", config_.log_level);

    // Load configuration from file if specified.
    if (!config_path_.empty()) {
        LOG_INFO("Loading configuration from: {}", config_path_);
        config_ = load_config(std::filesystem::path(config_path_));
    }

    // The selected subcommand's callback has already been invoked by
    // CLI11's parse(). Return 0 to indicate success.
    return 0;
}

auto App::cli() -> CLI::App& {
    return cli_;
}

auto App::config() -> Config& {
    return config_;
}

auto App::config() const -> const Config& {
    return config_;
}

void App::setup_commands() {
    register_gateway_command(cli_, config_);
    register_config_command(cli_, config_);
    register_version_command(cli_);
    register_status_command(cli_, config_);
}

} // namespace openclaw::cli
