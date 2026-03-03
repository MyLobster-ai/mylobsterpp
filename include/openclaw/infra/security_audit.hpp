#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

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

// ---------------------------------------------------------------------------
// v2026.3.2: Additional security audit functions
// ---------------------------------------------------------------------------

/// v2026.3.2: Validate bootstrap seed file is not a symlink/hardlink alias
/// outside the workspace boundary.
auto validate_bootstrap_seed_boundary(
    const std::filesystem::path& seed_path,
    const std::filesystem::path& workspace_root) -> bool;

/// v2026.3.2: Check for safe-regex patterns with quantified ambiguous alternation.
/// Returns true if the pattern is safe, false if it could cause catastrophic backtracking.
auto is_safe_regex_pattern(std::string_view pattern) -> bool;

/// v2026.3.2: Bound large regex-evaluation inputs for session-filter and log-redaction.
/// Returns the bounded input (truncated if too large).
auto bound_regex_input(std::string_view input, size_t max_bytes = 1024 * 1024)
    -> std::string_view;

/// v2026.3.2: Strip inbound metadata sentinels and validate JSON fences.
/// Tightens sentinel matching to prevent metadata injection attacks.
auto strip_inbound_metadata_sentinels(std::string_view content) -> std::string;

/// v2026.3.2: Expand Unicode angle-bracket homoglyph normalization for
/// external content marker folding. Normalizes various Unicode angle brackets
/// to ASCII < > to prevent bypass via homoglyphs.
auto normalize_external_content_markers(std::string_view content) -> std::string;

/// v2026.3.2: Harden skill installer metadata parsing.
/// Validates skill metadata JSON against injection attacks.
auto validate_skill_metadata(const nlohmann::json& metadata) -> bool;

/// v2026.3.2: Harden node platform classification against Unicode confusables.
/// Normalizes platform strings by stripping/replacing confusable characters.
auto normalize_platform_string(std::string_view platform) -> std::string;

/// v2026.3.2: Enforce 0600 permissions on rotated config backups.
auto harden_backup_permissions(const std::filesystem::path& backup_path) -> bool;

/// v2026.3.2: Remove eval-based command execution from logging utility.
/// Returns a sanitized command string safe for logging.
auto sanitize_log_command(std::string_view command) -> std::string;

} // namespace openclaw::infra
