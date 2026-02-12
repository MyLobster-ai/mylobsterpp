#include "openclaw/plugins/sdk.hpp"
#include "openclaw/core/logger.hpp"

#include <filesystem>

namespace openclaw::plugins {

PluginSDK::PluginSDK(const Config& config, std::filesystem::path data_dir)
    : data_dir_(std::move(data_dir))
{
    // Serialize the Config to JSON for read-only access by plugins.
    config_json_ = config;

    // Ensure the plugin data directory exists.
    std::error_code ec;
    std::filesystem::create_directories(data_dir_, ec);
    if (ec) {
        LOG_WARN("Failed to create plugin data directory {}: {}",
                 data_dir_.string(), ec.message());
    }
}

PluginSDK::~PluginSDK() = default;

PluginSDK::PluginSDK(PluginSDK&&) noexcept = default;
PluginSDK& PluginSDK::operator=(PluginSDK&&) noexcept = default;

auto PluginSDK::register_tool(std::unique_ptr<openclaw::agent::Tool> tool) -> void {
    if (!tool) {
        LOG_WARN("Attempted to register a null tool");
        return;
    }
    auto def = tool->definition();
    LOG_INFO("Plugin registered tool: {}", def.name);
    tools_.push_back(std::move(tool));
}

auto PluginSDK::register_channel(std::unique_ptr<openclaw::channels::Channel> channel) -> void {
    if (!channel) {
        LOG_WARN("Attempted to register a null channel");
        return;
    }
    LOG_INFO("Plugin registered channel: {} (type={})",
             channel->name(), channel->type());
    channels_.push_back(std::move(channel));
}

auto PluginSDK::log(std::string_view level, std::string_view message) -> void {
    if (level == "trace") {
        LOG_TRACE("[plugin] {}", message);
    } else if (level == "debug") {
        LOG_DEBUG("[plugin] {}", message);
    } else if (level == "info") {
        LOG_INFO("[plugin] {}", message);
    } else if (level == "warn") {
        LOG_WARN("[plugin] {}", message);
    } else if (level == "error") {
        LOG_ERROR("[plugin] {}", message);
    } else if (level == "fatal") {
        LOG_FATAL("[plugin] {}", message);
    } else {
        LOG_INFO("[plugin] {}", message);
    }
}

auto PluginSDK::config() const -> const json& {
    return config_json_;
}

auto PluginSDK::data_dir() const -> const std::filesystem::path& {
    return data_dir_;
}

auto PluginSDK::tools() -> std::vector<std::unique_ptr<openclaw::agent::Tool>>& {
    return tools_;
}

auto PluginSDK::channels() -> std::vector<std::unique_ptr<openclaw::channels::Channel>>& {
    return channels_;
}

} // namespace openclaw::plugins
