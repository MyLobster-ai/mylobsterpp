#include "openclaw/cli/commands.hpp"
#include "openclaw/core/logger.hpp"

#include <filesystem>
#include <iostream>
#include <regex>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <nlohmann/json.hpp>

// Version string; typically injected by CMake via -D, fallback to a default.
#ifndef OPENCLAW_VERSION_STRING
#define OPENCLAW_VERSION_STRING "2026.2.25"
#endif

namespace openclaw::cli {

using json = nlohmann::json;

namespace {

/// Recursively redacts sensitive values in a JSON object.
auto redact_config_json(json& j) -> void {
    static const std::vector<std::string> sensitive_keys = {
        "api_key", "bot_token", "access_token", "token", "secret",
        "signing_secret", "app_token", "verify_token",
    };

    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            bool is_sensitive = false;
            for (const auto& key : sensitive_keys) {
                if (it.key() == key) {
                    is_sensitive = true;
                    break;
                }
            }
            if (is_sensitive && it->is_string() && !it->get<std::string>().empty()) {
                *it = "***REDACTED***";
            } else {
                redact_config_json(*it);
            }
        }
    } else if (j.is_array()) {
        for (auto& elem : j) {
            redact_config_json(elem);
        }
    }
}

/// Wraps detected URLs with OSC 8 terminal hyperlinks for clickable links.
auto apply_osc8_hyperlinks(const std::string& text) -> std::string {
    static const std::regex url_re(R"((https?://[^\s<>"{}|\\^`\[\]]+))");
    return std::regex_replace(text, url_re, "\033]8;;$1\033\\$1\033]8;;\033\\");
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// gateway command
// ---------------------------------------------------------------------------

void register_gateway_command(CLI::App& app, Config& config) {
    auto* sub = app.add_subcommand("gateway", "Start the OpenClaw gateway server");

    uint16_t port = 0;
    sub->add_option("-p,--port", port, "Listen port (overrides config)")
        ->envname("OPENCLAW_PORT");

    std::string bind;
    sub->add_option("-b,--bind", bind, "Bind mode: loopback or all")
        ->envname("OPENCLAW_BIND");

    sub->callback([&config, sub, &port, &bind]() {
        Logger::init("openclaw", config.log_level);

        // Apply CLI overrides.
        if (port != 0) {
            config.gateway.port = port;
        }
        if (!bind.empty()) {
            config.gateway.bind = (bind == "all") ? BindMode::All : BindMode::Loopback;
        }

        LOG_INFO("Starting OpenClaw gateway on port {}", config.gateway.port);
        LOG_INFO("Bind mode: {}", config.gateway.bind == BindMode::Loopback
                                      ? "loopback" : "all");

        // Set up the Boost.Asio io_context and signal handling.
        boost::asio::io_context ioc;
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

        signals.async_wait([&ioc](auto ec, auto /*sig*/) {
            if (!ec) {
                LOG_INFO("Received shutdown signal");
                ioc.stop();
            }
        });

        // In a full build the GatewayServer would be constructed and
        // co_spawned here. For now, run the io_context so signals work.
        LOG_INFO("Gateway running. Press Ctrl+C to stop.");
        ioc.run();

        LOG_INFO("Gateway stopped.");
    });
}

// ---------------------------------------------------------------------------
// config command
// ---------------------------------------------------------------------------

void register_config_command(CLI::App& app, Config& config) {
    auto* sub = app.add_subcommand("config", "Show or validate configuration");

    bool validate_only = false;
    sub->add_flag("--validate", validate_only,
                  "Validate configuration without printing");

    std::string config_file;
    sub->add_option("-f,--file", config_file,
                    "Path to configuration file to inspect")
        ->check(CLI::ExistingFile);

    sub->callback([&config, &validate_only, &config_file]() {
        Config cfg = config;

        // If a specific file was given, load that instead.
        if (!config_file.empty()) {
            cfg = load_config(std::filesystem::path(config_file));
        }

        if (validate_only) {
            // If we got here the config parsed successfully.
            std::cout << "Configuration is valid.\n";
            return;
        }

        // Pretty-print the configuration as JSON (with secrets redacted).
        json j = cfg;
        redact_config_json(j);
        std::cout << j.dump(2) << "\n";
    });
}

// ---------------------------------------------------------------------------
// version command
// ---------------------------------------------------------------------------

void register_version_command(CLI::App& app) {
    auto* sub = app.add_subcommand("version", "Print version information");

    sub->callback([]() {
        std::cout << "openclaw " << OPENCLAW_VERSION_STRING << "\n";
        std::cout << "C++ standard: " << __cplusplus << "\n";
#if defined(__clang__)
        std::cout << "Compiler: clang " << __clang_major__ << "."
                  << __clang_minor__ << "." << __clang_patchlevel__ << "\n";
#elif defined(__GNUC__)
        std::cout << "Compiler: gcc " << __GNUC__ << "."
                  << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "\n";
#else
        std::cout << "Compiler: unknown\n";
#endif

#if defined(__APPLE__)
        std::cout << "Platform: macOS\n";
#elif defined(__linux__)
        std::cout << "Platform: Linux\n";
#else
        std::cout << "Platform: other\n";
#endif
    });
}

// ---------------------------------------------------------------------------
// status command
// ---------------------------------------------------------------------------

void register_status_command(CLI::App& app, Config& config) {
    auto* sub = app.add_subcommand("status", "Show status of a running gateway");

    std::string host = "127.0.0.1";
    sub->add_option("-H,--host", host, "Gateway host")
        ->default_val("127.0.0.1");

    uint16_t port = 0;
    sub->add_option("-p,--port", port, "Gateway port (default: from config)");

    sub->callback([&config, &host, &port]() {
        uint16_t target_port = port != 0 ? port : config.gateway.port;

        std::cout << "Checking gateway status at " << host << ":"
                  << target_port << " ...\n";

        // In a full build this would make an HTTP or WebSocket connection
        // to the gateway health endpoint and print the response.
        // For now, print the configuration-derived target.
        std::cout << "  Target: " << host << ":" << target_port << "\n";
        std::cout << "  Config log_level: " << config.log_level << "\n";
        std::cout << "  Providers configured: " << config.providers.size() << "\n";
        std::cout << "  Channels configured: " << config.channels.size() << "\n";
        std::cout << "  Plugins configured: " << config.plugins.size() << "\n";
        std::cout << "  Cron enabled: " << (config.cron.enabled ? "yes" : "no") << "\n";
    });
}

} // namespace openclaw::cli
