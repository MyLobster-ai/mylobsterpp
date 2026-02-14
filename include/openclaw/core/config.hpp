#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openclaw/core/types.hpp"

// std::optional serializer for nlohmann/json — enables NLOHMANN_DEFINE macros
// to work with optional fields via j.value("key", default_val)
namespace nlohmann {
template <typename T>
struct adl_serializer<std::optional<T>> {
    static void to_json(json& j, const std::optional<T>& opt) {
        if (opt.has_value()) {
            j = *opt;
        } else {
            j = nullptr;
        }
    }

    static void from_json(const json& j, std::optional<T>& opt) {
        if (j.is_null()) {
            opt = std::nullopt;
        } else {
            opt = j.get<T>();
        }
    }
};
} // namespace nlohmann

namespace openclaw {

struct AuthConfig {
    std::string method = "none";  // "none", "token", "tailscale"
    std::optional<std::string> token;
    std::optional<std::string> tailscale_authkey;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AuthConfig, method, token, tailscale_authkey)

struct TlsConfig {
    std::string cert_file;
    std::string key_file;
    std::optional<std::string> ca_file;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TlsConfig, cert_file, key_file, ca_file)

struct GatewayConfig {
    uint16_t port = 18789;
    BindMode bind = BindMode::Loopback;
    std::optional<AuthConfig> auth;
    std::optional<TlsConfig> tls;
    size_t max_connections = 100;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GatewayConfig, port, bind, max_connections)

struct ProviderConfig {
    std::string name;
    std::string api_key;
    std::optional<std::string> base_url;
    std::optional<std::string> model;
    std::optional<std::string> organization;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ProviderConfig, name, api_key, base_url, model, organization)

struct ChannelConfig {
    std::string type;
    bool enabled = false;
    json settings;
    std::optional<int> history_limit;  // Per-channel DM history compaction limit
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChannelConfig, type, enabled, settings, history_limit)

struct MemoryConfig {
    bool enabled = true;
    std::string store = "sqlite_vec";
    std::optional<std::string> db_path;
    size_t max_results = 10;
    double similarity_threshold = 0.7;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MemoryConfig, enabled, store, db_path, max_results, similarity_threshold)

struct BrowserConfig {
    bool enabled = false;
    size_t pool_size = 2;
    std::optional<std::string> chrome_path;
    int timeout_ms = 30000;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BrowserConfig, enabled, pool_size, chrome_path, timeout_ms)

struct SessionConfig {
    std::string store = "sqlite";
    std::optional<std::string> db_path;
    int ttl_seconds = 86400;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SessionConfig, store, db_path, ttl_seconds)

struct PluginConfig {
    std::string name;
    std::string path;
    bool enabled = true;
    json settings;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PluginConfig, name, path, enabled, settings)

struct CronConfig {
    bool enabled = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CronConfig, enabled)

struct Config {
    GatewayConfig gateway;
    std::vector<ProviderConfig> providers;
    std::vector<ChannelConfig> channels;
    MemoryConfig memory;
    BrowserConfig browser;
    SessionConfig sessions;
    std::vector<PluginConfig> plugins;
    CronConfig cron;
    std::string log_level = "info";
    std::optional<std::string> data_dir;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Config, gateway, providers, channels, memory, browser, sessions, plugins, cron, log_level, data_dir)

auto load_config(const std::filesystem::path& path) -> Config;
auto load_config_from_env() -> Config;
auto default_config() -> Config;
auto default_data_dir() -> std::filesystem::path;

/// Resolves `${VAR}` environment variable references in a string.
/// Supports `$${VAR}` escape (literal `${VAR}`).
auto resolve_env_refs(std::string_view input) -> std::string;

} // namespace openclaw
