#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "openclaw/agent/tool.hpp"
#include "openclaw/channels/channel.hpp"
#include "openclaw/core/config.hpp"
#include "openclaw/core/error.hpp"

namespace openclaw::plugins {

using json = nlohmann::json;

/// SDK context exposed to plugins during initialization.
///
/// Provides methods for plugins to register tools, channels, and other
/// extensions with the host application. Also provides access to
/// configuration, logging, and the plugin's data directory.
class PluginSDK {
public:
    /// Construct the SDK with references to the host's subsystems.
    /// @param config      The full host configuration (read-only).
    /// @param data_dir    Base data directory where the plugin may store files.
    PluginSDK(const Config& config, std::filesystem::path data_dir);

    ~PluginSDK();

    // Non-copyable, movable.
    PluginSDK(const PluginSDK&) = delete;
    PluginSDK& operator=(const PluginSDK&) = delete;
    PluginSDK(PluginSDK&&) noexcept;
    PluginSDK& operator=(PluginSDK&&) noexcept;

    /// Register a tool with the host agent.
    /// Ownership of the tool is transferred to the host.
    auto register_tool(std::unique_ptr<openclaw::agent::Tool> tool) -> void;

    /// Register a channel with the host.
    /// Ownership of the channel is transferred to the host.
    auto register_channel(std::unique_ptr<openclaw::channels::Channel> channel) -> void;

    /// Log a message at the given level through the host's logger.
    /// @param level   One of: "trace", "debug", "info", "warn", "error", "fatal".
    /// @param message The log message.
    auto log(std::string_view level, std::string_view message) -> void;

    /// Returns the full host configuration as JSON (read-only).
    [[nodiscard]] auto config() const -> const json&;

    /// Returns the directory where this plugin may persist data.
    /// The directory is guaranteed to exist after SDK construction.
    [[nodiscard]] auto data_dir() const -> const std::filesystem::path&;

    /// Returns all tools registered by plugins via this SDK.
    [[nodiscard]] auto tools() -> std::vector<std::unique_ptr<openclaw::agent::Tool>>&;

    /// Returns all channels registered by plugins via this SDK.
    [[nodiscard]] auto channels() -> std::vector<std::unique_ptr<openclaw::channels::Channel>>&;

private:
    json config_json_;
    std::filesystem::path data_dir_;
    std::vector<std::unique_ptr<openclaw::agent::Tool>> tools_;
    std::vector<std::unique_ptr<openclaw::channels::Channel>> channels_;
};

} // namespace openclaw::plugins
