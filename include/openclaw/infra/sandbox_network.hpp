#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace openclaw::infra {

/// Reasons why a Docker network mode may be blocked.
enum class NetworkModeBlockReason {
    Host,                    // "host" mode gives full network access
    ContainerNamespaceJoin,  // "container:<id>" joins another container's network
};

/// Normalizes a network mode string (lowercase, trimmed).
auto normalize_network_mode(std::string_view mode) -> std::string;

/// Returns the reason a network mode is blocked, or nullopt if it is safe.
/// Safe modes include: "bridge", "none", custom named networks.
/// Dangerous modes: "host", "container:<id>".
auto get_blocked_network_mode_reason(std::string_view mode)
    -> std::optional<NetworkModeBlockReason>;

/// Returns true if the network mode is dangerous (host or container namespace join).
auto is_dangerous_network_mode(std::string_view mode) -> bool;

/// Validates sandbox network security, respecting the break-glass override.
/// Returns true if the network mode is safe to use.
/// When dangerously_allow_container_namespace_join is true, container:<id> is allowed.
auto validate_sandbox_network_mode(std::string_view mode,
                                    bool dangerously_allow_container_namespace_join = false)
    -> bool;

} // namespace openclaw::infra
