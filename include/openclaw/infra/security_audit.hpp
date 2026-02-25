#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "openclaw/core/config.hpp"

namespace openclaw::infra {

/// A security finding from the multi-user heuristic check.
struct SecurityFinding {
    std::string category;   // e.g. "multi_user", "tool_exposure"
    std::string severity;   // "info", "warning", "critical"
    std::string message;
    std::string remediation;
};

/// Collects multi-user heuristic findings from the config.
/// Returns findings about open group/DM policies, wildcard allowlists,
/// and sandbox mode vs tool exposure mismatches.
auto collect_multi_user_findings(const Config& config) -> std::vector<SecurityFinding>;

/// Lists signals that the config may be used in a multi-user context.
auto list_potential_multi_user_signals(const Config& config) -> std::vector<std::string>;

/// Collects findings about risky tool exposure for the given config.
auto collect_risky_tool_exposure(const Config& config) -> std::vector<SecurityFinding>;

} // namespace openclaw::infra
