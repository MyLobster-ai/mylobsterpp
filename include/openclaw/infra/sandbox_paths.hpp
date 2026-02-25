#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "openclaw/core/error.hpp"

namespace openclaw::infra {

/// Asserts that the file at `path` has no additional hard links (nlink > 1).
/// Prevents sandbox escape via hard link chains to files outside the sandbox.
auto assert_no_hardlinked_final_path(const std::filesystem::path& path)
    -> openclaw::Result<void>;

/// Resolves a sandbox-relative path for temporary media files,
/// ensuring the result stays within the allowed media directory.
auto resolve_allowed_tmp_media_path(const std::filesystem::path& base,
                                     std::string_view relative)
    -> openclaw::Result<std::filesystem::path>;

/// Strips a leading '@' from a path string.
/// Some tools use @-prefixed paths (e.g., @/path/to/file) as workspace references;
/// this must be normalized before boundary checks to prevent bypasses.
auto normalize_at_prefix(std::string_view path) -> std::string;

/// Canonicalizes a bind-mount source path by resolving through
/// the nearest existing ancestor. This handles cases where symlink-parent
/// combined with a non-existent leaf could bypass containment checks.
auto canonicalize_bind_mount_source(const std::filesystem::path& path)
    -> openclaw::Result<std::filesystem::path>;

} // namespace openclaw::infra
