#include "openclaw/infra/path_alias_guards.hpp"

#include "openclaw/core/logger.hpp"

#include <sys/stat.h>
#include <unistd.h>

namespace openclaw::infra {

namespace fs = std::filesystem;

namespace {

/// Returns true if `child` is contained within `root` (both must be canonical).
auto is_contained_in(const fs::path& child, const fs::path& root) -> bool {
    auto child_str = child.string();
    auto root_str = root.string();
    if (root_str.back() != '/') root_str += '/';
    return child_str.starts_with(root_str) || child_str == root.string();
}

/// Returns true if `path` is contained in any of the workspace roots.
auto is_within_any_root(const fs::path& path,
                        const std::vector<fs::path>& roots) -> bool {
    for (const auto& root : roots) {
        std::error_code ec;
        auto canonical_root = fs::canonical(root, ec);
        if (ec) continue;
        if (is_contained_in(path, canonical_root)) {
            return true;
        }
    }
    return false;
}

struct StatIdentity {
    dev_t device;
    ino_t inode;
    nlink_t nlink;
};

auto lstat_identity(const fs::path& path) -> Result<StatIdentity> {
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) {
        return std::unexpected(make_error(
            ErrorCode::IoError,
            "lstat failed",
            path.string() + ": " + std::string(strerror(errno))));
    }
    return StatIdentity{st.st_dev, st.st_ino, st.st_nlink};
}

auto stat_identity(const fs::path& path) -> Result<StatIdentity> {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) {
        return std::unexpected(make_error(
            ErrorCode::IoError,
            "stat failed",
            path.string() + ": " + std::string(strerror(errno))));
    }
    return StatIdentity{st.st_dev, st.st_ino, st.st_nlink};
}

} // anonymous namespace

auto assert_no_path_alias_escape(
    const fs::path& path,
    const std::vector<fs::path>& workspace_roots) -> VoidResult
{
    if (workspace_roots.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "No workspace roots provided for path alias check"));
    }

    // Canonicalize the full path first to check final containment.
    // This resolves platform symlinks like /var -> /private/var on macOS.
    std::error_code canon_ec;
    auto canonical_path = fs::canonical(path, canon_ec);
    if (!canon_ec) {
        if (!is_within_any_root(canonical_path, workspace_roots)) {
            LOG_WARN("Path escapes workspace after canonicalization: {} -> {}",
                     path.string(), canonical_path.string());
            return std::unexpected(make_error(
                ErrorCode::Forbidden,
                "Path escapes workspace boundary",
                path.string() + " -> " + canonical_path.string()));
        }
    }

    // Walk each component to detect intermediate symlinks that escape the workspace.
    // Only flag symlinks whose targets aren't ancestors of the workspace (true escapes),
    // not platform-level symlinks like /var -> /private/var.
    fs::path accumulated;
    for (const auto& component : path) {
        accumulated /= component;

        // Skip the root component (e.g., "/")
        if (accumulated == component && component == "/") {
            continue;
        }

        std::error_code ec;
        if (!fs::exists(accumulated, ec) || ec) {
            // Path component doesn't exist yet — stop walking
            break;
        }

        // Check if this component is a symlink
        if (fs::is_symlink(accumulated, ec) && !ec) {
            // Resolve the symlink target
            auto target = fs::read_symlink(accumulated, ec);
            if (ec) {
                return std::unexpected(make_error(
                    ErrorCode::IoError,
                    "Failed to read symlink",
                    accumulated.string() + ": " + ec.message()));
            }

            // Resolve to absolute path
            fs::path resolved = target.is_absolute()
                ? target
                : accumulated.parent_path() / target;

            auto resolved_canonical = fs::canonical(resolved, ec);
            if (ec) {
                return std::unexpected(make_error(
                    ErrorCode::IoError,
                    "Failed to canonicalize symlink target",
                    resolved.string() + ": " + ec.message()));
            }

            // Skip if the resolved target is an ancestor of any workspace root.
            // System-level symlinks like /var -> /private/var are ancestors, not escapes.
            bool is_ancestor = false;
            for (const auto& root : workspace_roots) {
                std::error_code root_ec;
                auto cr = fs::canonical(root, root_ec);
                if (root_ec) continue;
                auto cr_str = cr.string();
                auto rt_str = resolved_canonical.string();
                if (rt_str.back() != '/') rt_str += '/';
                if (cr_str.starts_with(rt_str) || cr == resolved_canonical) {
                    is_ancestor = true;
                    break;
                }
            }
            if (is_ancestor) continue;

            // Check if the resolved target is within any workspace root
            if (!is_within_any_root(resolved_canonical, workspace_roots)) {
                LOG_WARN("Path alias escape detected: {} -> {} escapes workspace",
                         accumulated.string(), resolved_canonical.string());
                return std::unexpected(make_error(
                    ErrorCode::Forbidden,
                    "Symlink escapes workspace boundary",
                    accumulated.string() + " -> " + resolved_canonical.string()));
            }
        }
    }

    return {};
}

auto assert_no_hardlinked_final_path_strict(
    const fs::path& path,
    PathAliasPolicy policy) -> VoidResult
{
    // Step 1: lstat (no symlink follow)
    auto lstat_result = lstat_identity(path);
    if (!lstat_result) {
        return std::unexpected(lstat_result.error());
    }

    // Step 2: stat (follows symlinks)
    auto stat_result = stat_identity(path);
    if (!stat_result) {
        return std::unexpected(stat_result.error());
    }

    // Step 3: realpath + stat
    std::error_code ec;
    auto canonical = fs::canonical(path, ec);
    if (ec) {
        return std::unexpected(make_error(
            ErrorCode::IoError,
            "Failed to canonicalize path",
            path.string() + ": " + ec.message()));
    }

    auto realpath_stat_result = stat_identity(canonical);
    if (!realpath_stat_result) {
        return std::unexpected(realpath_stat_result.error());
    }

    // TOCTOU detection: verify inode/device identity is stable across all three checks
    if (lstat_result->inode != stat_result->inode ||
        lstat_result->device != stat_result->device) {
        // This is expected for symlinks — the lstat inode is the symlink itself.
        // Only flag as TOCTOU if the stat and realpath_stat differ.
    }

    if (stat_result->inode != realpath_stat_result->inode ||
        stat_result->device != realpath_stat_result->device) {
        LOG_WARN("TOCTOU race detected on {}: inode/device changed between stat calls",
                 path.string());
        return std::unexpected(make_error(
            ErrorCode::Forbidden,
            "TOCTOU race detected: file identity changed during verification",
            path.string()));
    }

    // Hardlink check: nlink > 1 on the resolved file means it has hard links
    if (realpath_stat_result->nlink > 1) {
        LOG_WARN("Hardlinked file detected: {} has {} links",
                 canonical.string(), realpath_stat_result->nlink);

        if (policy == PathAliasPolicy::UnlinkTarget) {
            // Attempt to unlink the target path (not the canonical)
            if (::unlink(path.c_str()) != 0) {
                return std::unexpected(make_error(
                    ErrorCode::Forbidden,
                    "Hardlinked file detected and unlink failed",
                    path.string() + ": " + std::string(strerror(errno))));
            }
            LOG_INFO("Unlinked hardlinked file: {}", path.string());
            return {};
        }

        return std::unexpected(make_error(
            ErrorCode::Forbidden,
            "Hardlinked file rejected (nlink > 1)",
            canonical.string() + " has " +
                std::to_string(realpath_stat_result->nlink) + " links"));
    }

    return {};
}

auto assert_path_safe(
    const fs::path& path,
    const std::vector<fs::path>& workspace_roots,
    PathAliasPolicy policy) -> VoidResult
{
    auto escape_check = assert_no_path_alias_escape(path, workspace_roots);
    if (!escape_check) {
        return escape_check;
    }

    // Only check hardlinks on existing regular files
    std::error_code ec;
    if (fs::exists(path, ec) && !ec && fs::is_regular_file(path, ec) && !ec) {
        auto hardlink_check = assert_no_hardlinked_final_path_strict(path, policy);
        if (!hardlink_check) {
            return hardlink_check;
        }
    }

    return {};
}

} // namespace openclaw::infra
