#include "openclaw/infra/sandbox_paths.hpp"

#include "openclaw/core/logger.hpp"

namespace openclaw::infra {

namespace fs = std::filesystem;

auto assert_no_hardlinked_final_path(const fs::path& path)
    -> openclaw::Result<void> {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return std::unexpected(make_error(ErrorCode::NotFound,
            "Path does not exist", path.string()));
    }

    auto link_count = fs::hard_link_count(path, ec);
    if (ec) {
        return std::unexpected(make_error(ErrorCode::IoError,
            "Failed to get hard link count", path.string() + ": " + ec.message()));
    }

    // Regular files should have nlink == 1; directories typically > 1
    if (fs::is_regular_file(path, ec) && link_count > 1) {
        return std::unexpected(make_error(ErrorCode::Forbidden,
            "Hard link detected: file has multiple links",
            path.string() + " (nlink=" + std::to_string(link_count) + ")"));
    }

    return {};
}

auto resolve_allowed_tmp_media_path(const fs::path& base,
                                     std::string_view relative)
    -> openclaw::Result<fs::path> {
    // Normalize the @-prefix first
    auto normalized = normalize_at_prefix(relative);

    // Construct the full path
    auto full = base / normalized;

    // Canonicalize to resolve any .. or symlinks
    std::error_code ec;
    auto canonical_base = fs::canonical(base, ec);
    if (ec) {
        return std::unexpected(make_error(ErrorCode::IoError,
            "Failed to canonicalize base path", base.string()));
    }

    // For the full path, use weakly_canonical to handle non-existent paths
    auto canonical_full = fs::weakly_canonical(full, ec);
    if (ec) {
        return std::unexpected(make_error(ErrorCode::IoError,
            "Failed to resolve path", full.string()));
    }

    // Containment check
    auto base_str = canonical_base.string();
    auto full_str = canonical_full.string();
    if (!full_str.starts_with(base_str)) {
        return std::unexpected(make_error(ErrorCode::Forbidden,
            "Path traversal: resolved path escapes sandbox",
            full_str + " not within " + base_str));
    }

    return canonical_full;
}

auto normalize_at_prefix(std::string_view path) -> std::string {
    if (path.starts_with("@")) {
        return std::string(path.substr(1));
    }
    return std::string(path);
}

auto canonicalize_bind_mount_source(const fs::path& path)
    -> openclaw::Result<fs::path> {
    std::error_code ec;

    // If the full path exists, just canonicalize it
    if (fs::exists(path, ec)) {
        auto canonical = fs::canonical(path, ec);
        if (ec) {
            return std::unexpected(make_error(ErrorCode::IoError,
                "Failed to canonicalize path", path.string() + ": " + ec.message()));
        }
        return canonical;
    }

    // Walk up to find the nearest existing ancestor
    auto current = path;
    auto leaf = fs::path{};

    while (!current.empty() && !fs::exists(current, ec)) {
        leaf = current.filename() / leaf;
        current = current.parent_path();
    }

    if (current.empty()) {
        return std::unexpected(make_error(ErrorCode::NotFound,
            "No existing ancestor found for bind mount source", path.string()));
    }

    // Canonicalize the existing ancestor
    auto canonical_ancestor = fs::canonical(current, ec);
    if (ec) {
        return std::unexpected(make_error(ErrorCode::IoError,
            "Failed to canonicalize ancestor", current.string()));
    }

    // Reconstruct with the non-existent leaf
    return canonical_ancestor / leaf;
}

} // namespace openclaw::infra
