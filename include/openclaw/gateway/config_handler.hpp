#pragma once

#include <filesystem>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "openclaw/core/config.hpp"
#include "openclaw/gateway/protocol.hpp"

namespace openclaw::gateway {

/// Manages runtime configuration as a mutable JSON document.
/// Supports dot-path navigation, atomic patches, and persistence.
class RuntimeConfig {
public:
    explicit RuntimeConfig(const Config& initial_config);

    /// Get value at dot-separated path. Returns null if not found.
    [[nodiscard]] auto get(std::string_view path) const -> json;

    /// Set value at dot-separated path.
    void set(std::string_view path, const json& value);

    /// Apply a batch of patches with optimistic concurrency.
    /// Returns false if baseHash doesn't match current hash.
    auto patch(const std::vector<std::pair<std::string, json>>& patches,
               const std::string& base_hash) -> bool;

    /// Get SHA256 hash of the current config.
    [[nodiscard]] auto hash() const -> std::string;

    /// Get the full config as JSON.
    [[nodiscard]] auto to_json() const -> json;

    /// Reset to default configuration.
    void reset();

    /// Set persistence path. If set, changes are auto-saved.
    void set_persist_path(const std::filesystem::path& path);

    /// List all top-level config keys.
    [[nodiscard]] auto list_keys() const -> std::vector<std::string>;

private:
    void persist() const;
    static auto navigate(json& root, std::string_view path, bool create) -> json*;
    static auto navigate(const json& root, std::string_view path) -> const json*;
    static auto compute_hash(const json& j) -> std::string;

    mutable std::mutex mutex_;
    json config_;
    json default_config_;
    std::filesystem::path persist_path_;
};

/// Registers config.get, config.set, config.patch, config.list,
/// config.reset, config.export, config.import handlers on the protocol.
void register_config_handlers(Protocol& protocol, RuntimeConfig& runtime_config);

} // namespace openclaw::gateway
