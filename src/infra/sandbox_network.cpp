#include "openclaw/infra/sandbox_network.hpp"

#include <algorithm>
#include <cctype>

namespace openclaw::infra {

auto normalize_network_mode(std::string_view mode) -> std::string {
    std::string result(mode);

    // Trim leading/trailing whitespace
    auto start = result.find_first_not_of(" \t\n\r");
    auto end = result.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    result = result.substr(start, end - start + 1);

    // Lowercase
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

auto get_blocked_network_mode_reason(std::string_view mode)
    -> std::optional<NetworkModeBlockReason> {
    auto normalized = normalize_network_mode(mode);

    if (normalized == "host") {
        return NetworkModeBlockReason::Host;
    }

    if (normalized.starts_with("container:")) {
        return NetworkModeBlockReason::ContainerNamespaceJoin;
    }

    return std::nullopt;
}

auto is_dangerous_network_mode(std::string_view mode) -> bool {
    return get_blocked_network_mode_reason(mode).has_value();
}

auto validate_sandbox_network_mode(std::string_view mode,
                                    bool dangerously_allow_container_namespace_join) -> bool {
    auto reason = get_blocked_network_mode_reason(mode);
    if (!reason.has_value()) return true;  // Safe mode

    // Host mode is always blocked — no break-glass override
    if (*reason == NetworkModeBlockReason::Host) return false;

    // Container namespace join can be allowed with explicit break-glass flag
    if (*reason == NetworkModeBlockReason::ContainerNamespaceJoin) {
        return dangerously_allow_container_namespace_join;
    }

    return false;
}

} // namespace openclaw::infra
