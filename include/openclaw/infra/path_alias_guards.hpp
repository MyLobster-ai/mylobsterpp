#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "openclaw/core/error.hpp"

namespace openclaw::infra {

/// Decode a single percent-encoded byte (%XX) in a string.
/// Returns the decoded string. Does NOT handle '+' → ' '.
auto uri_decode_percent(std::string_view input) -> std::string;

/// Iteratively decode percent-encoding up to `max_passes` times.
/// Stops early if a pass produces no change (stable).
auto iterative_uri_decode(std::string_view input, int max_passes = 3) -> std::string;

/// Returns true if the string contains malformed percent-encoding:
///   - %XX where X is not a valid hex digit
///   - %00 (null byte injection)
///   - Trailing '%' without two hex digits
auto has_malformed_percent_encoding(std::string_view input) -> bool;

/// Policy for handling path alias escape detection.
enum class PathAliasPolicy {
    /// Strict: reject any symlink or hardlink escape. No remediation.
    Strict,
    /// UnlinkTarget: on hardlink detection, attempt to unlink the target
    /// before returning. Falls back to Strict error if unlink fails.
    UnlinkTarget,
};

/// Walks every component of `path` via lstat(). Rejects if any intermediate
/// symlink resolves outside any of the `workspace_roots`.
///
/// This prevents symlink-based directory traversal attacks where an attacker
/// creates a symlink chain that eventually escapes the workspace boundary.
///
/// Returns an error with ErrorCode::Forbidden if an escape is detected,
/// or ErrorCode::IoError if any stat call fails.
[[nodiscard]] auto assert_no_path_alias_escape(
    const std::filesystem::path& path,
    const std::vector<std::filesystem::path>& workspace_roots) -> VoidResult;

/// Triple-stat TOCTOU-resistant hardlink check on the final path component.
///
/// Performs three stat operations in sequence:
///   1. lstat() — gets link count without following symlinks
///   2. stat()  — gets link count following symlinks
///   3. realpath() + stat() — resolves to canonical path and re-checks
///
/// Rejects if nlink > 1 at any step (indicates hardlink).
/// Rejects if inode or device changes between any two steps (indicates TOCTOU race).
///
/// @param path         File path to check.
/// @param policy       How to handle detected hardlinks.
/// @return VoidResult  Success if file is not hardlinked and identity is stable.
[[nodiscard]] auto assert_no_hardlinked_final_path_strict(
    const std::filesystem::path& path,
    PathAliasPolicy policy = PathAliasPolicy::Strict) -> VoidResult;

/// Combined guard: runs both symlink escape and hardlink checks.
/// Convenience wrapper for security-critical file operations.
[[nodiscard]] auto assert_path_safe(
    const std::filesystem::path& path,
    const std::vector<std::filesystem::path>& workspace_roots,
    PathAliasPolicy policy = PathAliasPolicy::Strict) -> VoidResult;

} // namespace openclaw::infra
