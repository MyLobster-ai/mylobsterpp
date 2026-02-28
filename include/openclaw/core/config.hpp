#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openclaw/core/secrets.hpp"
#include "openclaw/core/types.hpp"

// std::optional serializer is defined in secrets.hpp (included above).

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
    std::string http_security_hsts;  // v2026.2.24: HSTS header value (empty = disabled)
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GatewayConfig, port, bind, max_connections, http_security_hsts)

struct ProviderConfig {
    std::string name;
    std::string api_key;
    std::optional<std::string> base_url;
    std::optional<std::string> model;
    std::optional<std::string> organization;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ProviderConfig, name, api_key, base_url, model, organization)

/// v2026.2.26: Thread binding policy for channel sessions.
struct ThreadBindingConfig {
    bool enabled = true;           // Whether thread binding is active
    bool spawn_subagent = true;    // Allow spawning sub-agents in threads
    bool spawn_acp = true;         // Allow spawning ACP in threads
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ThreadBindingConfig, enabled, spawn_subagent, spawn_acp)

struct ChannelConfig {
    std::string type;
    bool enabled = false;
    json settings;
    std::optional<int> history_limit;  // Per-channel DM history compaction limit
    std::optional<ThreadBindingConfig> thread_binding;  // v2026.2.26
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChannelConfig, type, enabled, settings, history_limit, thread_binding)

struct MemoryConfig {
    bool enabled = true;
    std::string store = "sqlite_vec";
    std::optional<std::string> db_path;
    size_t max_results = 10;
    double similarity_threshold = 0.7;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MemoryConfig, enabled, store, db_path, max_results, similarity_threshold)

struct SsrfPolicyConfig {
    std::optional<bool> allow_private_network;               // legacy key
    std::optional<bool> dangerously_allow_private_network;   // canonical key (v2026.2.23+)
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SsrfPolicyConfig, allow_private_network, dangerously_allow_private_network)

/// Resolves the effective SSRF private-network policy.
/// If neither key is explicitly set, defaults to true (trusted-network mode).
inline auto resolve_ssrf_allow_private(const SsrfPolicyConfig& policy) -> bool {
    bool has_explicit = policy.allow_private_network.has_value() ||
                        policy.dangerously_allow_private_network.has_value();
    if (!has_explicit) return true;  // v2026.2.23 default: trusted-network
    // Canonical key takes precedence
    if (policy.dangerously_allow_private_network.has_value())
        return *policy.dangerously_allow_private_network;
    return *policy.allow_private_network;
}

struct BrowserConfig {
    bool enabled = false;
    size_t pool_size = 2;
    std::optional<std::string> chrome_path;
    int timeout_ms = 30000;
    SsrfPolicyConfig ssrf_policy;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BrowserConfig, enabled, pool_size, chrome_path, timeout_ms, ssrf_policy)

struct SessionConfig {
    std::string store = "sqlite";
    std::optional<std::string> db_path;
    int ttl_seconds = 86400;
    int compaction_floor_tokens = 0;  // Minimum tokens to keep after compaction (0 = no floor)
    std::optional<ThreadBindingConfig> thread_binding;  // v2026.2.26
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SessionConfig, store, db_path, ttl_seconds, compaction_floor_tokens, thread_binding)

struct PluginConfig {
    std::string name;
    std::string path;
    bool enabled = true;
    json settings;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PluginConfig, name, path, enabled, settings)

struct CronConfig {
    bool enabled = false;
    std::optional<int> default_stagger_ms;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CronConfig, enabled, default_stagger_ms)

struct HeartbeatConfig {
    std::string target = "none";  // v2026.2.24: default "none" (was "last")
    std::optional<std::string> cron_expression;
    std::optional<std::string> message;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HeartbeatConfig, target, cron_expression, message)

struct SandboxDockerSettings {
    bool dangerously_allow_container_namespace_join = false;
    std::optional<std::string> network_mode;
    std::optional<std::vector<std::string>> bind_mounts;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SandboxDockerSettings, dangerously_allow_container_namespace_join, network_mode, bind_mounts)

struct SandboxConfig {
    bool enabled = false;
    SandboxDockerSettings docker;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SandboxConfig, enabled, docker)

struct HttpSecurityHeaders {
    std::optional<std::string> strict_transport_security;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HttpSecurityHeaders, strict_transport_security)

struct SubagentConfig {
    std::optional<int> max_spawn_depth;        // 1-5, default 1
    std::optional<int> max_children_per_agent;  // 1-20, default 5
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SubagentConfig, max_spawn_depth, max_children_per_agent)

struct ImageConfig {
    std::optional<int> max_dimension_px;  // default 1200
    std::optional<int> max_bytes;         // default 5MB
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ImageConfig, max_dimension_px, max_bytes)

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
    std::optional<SubagentConfig> subagents;
    std::optional<ImageConfig> image;
    std::optional<std::map<std::string, std::string>> model_by_channel;
    HeartbeatConfig heartbeat;
    SandboxConfig sandbox;
    HttpSecurityHeaders http_security;
    std::optional<SecretsConfig> secrets;  // v2026.2.26: external secrets management
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Config, gateway, providers, channels, memory, browser, sessions, plugins, cron, log_level, data_dir, subagents, image, model_by_channel, heartbeat, sandbox, http_security, secrets)

/// v2026.2.26: Resolve thread binding policy with cascade:
/// session config > channel config > global default.
inline auto resolve_thread_binding_policy(
    const std::optional<ThreadBindingConfig>& session_override,
    const std::optional<ThreadBindingConfig>& channel_override)
    -> ThreadBindingConfig
{
    if (session_override) return *session_override;
    if (channel_override) return *channel_override;
    return ThreadBindingConfig{};  // global defaults
}

auto load_config(const std::filesystem::path& path) -> Config;
auto load_config_from_env() -> Config;
auto default_config() -> Config;
auto default_data_dir() -> std::filesystem::path;

/// Resolves `${VAR}` environment variable references in a string.
/// Supports `$${VAR}` escape (literal `${VAR}`).
auto resolve_env_refs(std::string_view input) -> std::string;

} // namespace openclaw
