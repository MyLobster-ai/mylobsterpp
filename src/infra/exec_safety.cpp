#include "openclaw/infra/exec_safety.hpp"

#include "openclaw/core/logger.hpp"

#include <filesystem>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace openclaw::infra {

auto unwrap_shell_wrapper_argv(const std::vector<std::string>& argv)
    -> std::optional<size_t> {
    size_t idx = 0;
    int depth = 0;

    while (idx < argv.size() && depth < kMaxUnwrapDepth) {
        auto binary = std::filesystem::path(argv[idx]).filename().string();
        if (!is_shell_wrapper(binary)) {
            return idx;
        }
        ++idx;
        ++depth;

        // Skip flags (arguments starting with -)
        while (idx < argv.size() && argv[idx].starts_with("-")) {
            // Special case: -c flag means next arg is inline command
            if (argv[idx] == "-c") {
                return idx + 1;  // The inline command follows -c
            }
            ++idx;
        }
    }

    // Depth cap exceeded — fail closed
    if (depth >= kMaxUnwrapDepth) {
        return std::nullopt;
    }

    return idx;
}

auto resolve_inline_command_token_index(const std::vector<std::string>& argv)
    -> std::optional<size_t> {
    for (size_t i = 0; i < argv.size(); ++i) {
        if (argv[i] == "-c" && i + 1 < argv.size()) {
            return i + 1;
        }
    }
    return std::nullopt;
}

auto has_trailing_positional_argv(const std::vector<std::string>& argv,
                                   size_t command_index) -> bool {
    // Check if there are non-flag arguments after the command
    for (size_t i = command_index + 1; i < argv.size(); ++i) {
        if (!argv[i].starts_with("-")) {
            return true;
        }
    }
    return false;
}

auto validate_system_run_consistency(const std::vector<std::string>& argv,
                                      std::string_view declared_command) -> bool {
    if (argv.empty()) return false;

    auto resolved_idx = unwrap_shell_wrapper_argv(argv);
    if (!resolved_idx.has_value()) {
        // Wrapper depth cap exceeded — fail closed
        return false;
    }

    if (*resolved_idx >= argv.size()) {
        return false;
    }

    // Check if the resolved command matches the declared command
    auto resolved_binary = std::filesystem::path(argv[*resolved_idx]).filename().string();
    auto declared_binary = std::filesystem::path(std::string(declared_command)).filename().string();

    return resolved_binary == declared_binary;
}

auto harden_approved_execution_paths(const std::filesystem::path& cwd,
                                      const std::filesystem::path& executable)
    -> bool
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // 1. Reject symlink cwd — prevents cwd-swap attacks where the attacker
    //    replaces the working directory with a symlink between approval and execution
    if (fs::is_symlink(cwd, ec)) {
        LOG_WARN("Exec hardening: cwd is a symlink, rejecting: {}", cwd.string());
        return false;
    }
    if (ec) {
        LOG_WARN("Exec hardening: failed to check cwd symlink status: {}", ec.message());
        return false;
    }

    // Verify cwd exists and is a directory
    if (!fs::is_directory(cwd, ec) || ec) {
        LOG_WARN("Exec hardening: cwd is not a directory: {}", cwd.string());
        return false;
    }

#ifndef _WIN32
    // 2. Triple-stat the executable for TOCTOU prevention
    struct stat lstat_buf{};
    if (::lstat(executable.c_str(), &lstat_buf) != 0) {
        LOG_WARN("Exec hardening: lstat failed on executable: {}",
                 executable.string());
        return false;
    }

    struct stat stat_buf{};
    if (::stat(executable.c_str(), &stat_buf) != 0) {
        LOG_WARN("Exec hardening: stat failed on executable: {}",
                 executable.string());
        return false;
    }

    // 3. Canonicalize and re-stat
    auto canonical = fs::canonical(executable, ec);
    if (ec) {
        LOG_WARN("Exec hardening: canonical failed on executable: {} ({})",
                 executable.string(), ec.message());
        return false;
    }

    struct stat realpath_buf{};
    if (::stat(canonical.c_str(), &realpath_buf) != 0) {
        LOG_WARN("Exec hardening: stat failed on canonical executable: {}",
                 canonical.string());
        return false;
    }

    // 4. Verify inode identity is stable across stat and realpath+stat
    if (stat_buf.st_ino != realpath_buf.st_ino ||
        stat_buf.st_dev != realpath_buf.st_dev) {
        LOG_WARN("Exec hardening: TOCTOU detected on executable {} "
                 "(inode {} vs {}, dev {} vs {})",
                 executable.string(),
                 stat_buf.st_ino, realpath_buf.st_ino,
                 stat_buf.st_dev, realpath_buf.st_dev);
        return false;
    }

    LOG_DEBUG("Exec hardening: paths verified (cwd={}, exe={})",
              cwd.string(), canonical.string());
#else
    // Windows: basic existence check only
    if (!fs::exists(executable, ec) || ec) {
        LOG_WARN("Exec hardening: executable not found: {}", executable.string());
        return false;
    }
#endif

    return true;
}

// ---------------------------------------------------------------------------
// v2026.3.2: Escape metacharacters in path-pattern literals for exec approvals
// ---------------------------------------------------------------------------

auto escape_regex_metacharacters(std::string_view input) -> std::string {
    std::string result;
    result.reserve(input.size() * 2);
    for (char c : input) {
        switch (c) {
            case '.': case '+': case '*': case '?':
            case '(': case ')': case '[': case ']':
            case '{': case '}': case '\\': case '^':
            case '$': case '|':
                result += '\\';
                result += c;
                break;
            default:
                result += c;
                break;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// v2026.3.2: Revalidate approval-bound cwd identity before execution
// ---------------------------------------------------------------------------

auto revalidate_approval_cwd_identity(
    const std::filesystem::path& original_cwd,
    const std::filesystem::path& current_cwd) -> bool
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // Canonicalize both paths
    auto canonical_original = fs::canonical(original_cwd, ec);
    if (ec) {
        LOG_WARN("Exec revalidation: cannot canonicalize original cwd: {} ({})",
                 original_cwd.string(), ec.message());
        return false;
    }

    auto canonical_current = fs::canonical(current_cwd, ec);
    if (ec) {
        LOG_WARN("Exec revalidation: cannot canonicalize current cwd: {} ({})",
                 current_cwd.string(), ec.message());
        return false;
    }

    if (canonical_original != canonical_current) {
        LOG_WARN("Exec revalidation: cwd changed between approval and execution: {} vs {}",
                 canonical_original.string(), canonical_current.string());
        return false;
    }

#ifndef _WIN32
    // Verify inode identity of cwd hasn't changed (TOCTOU protection)
    struct stat original_stat{}, current_stat{};
    if (::stat(canonical_original.c_str(), &original_stat) != 0 ||
        ::stat(canonical_current.c_str(), &current_stat) != 0) {
        LOG_WARN("Exec revalidation: stat failed on cwd");
        return false;
    }

    if (original_stat.st_ino != current_stat.st_ino ||
        original_stat.st_dev != current_stat.st_dev) {
        LOG_WARN("Exec revalidation: cwd inode changed between approval and execution");
        return false;
    }
#endif

    return true;
}

// ---------------------------------------------------------------------------
// v2026.3.2: Preserve shell/dispatch-wrapper argv during approval validation
// ---------------------------------------------------------------------------

auto validate_with_preserved_argv(
    const std::vector<std::string>& argv,
    std::string_view declared_command,
    bool preserve_wrappers) -> bool
{
    if (!preserve_wrappers) {
        return validate_system_run_consistency(argv, declared_command);
    }

    // When preserving wrappers, validate the full argv chain
    // instead of unwrapping to the inner command
    if (argv.empty()) return false;

    // The first element should be the declared command (or a known wrapper)
    auto first_binary = std::filesystem::path(argv[0]).filename().string();
    auto declared_binary = std::filesystem::path(std::string(declared_command)).filename().string();

    if (first_binary == declared_binary) return true;

    // Check if it's a known wrapper followed by the declared command
    if (is_shell_wrapper(first_binary)) {
        return validate_system_run_consistency(argv, declared_command);
    }

    return false;
}

#ifdef _WIN32
// ---------------------------------------------------------------------------
// v2026.3.2: Windows ACPX spawn - resolve .cmd/.bat wrappers via PATH/PATHEXT
// ---------------------------------------------------------------------------

auto resolve_windows_cmd_wrapper(std::string_view command)
    -> std::optional<std::filesystem::path>
{
    namespace fs = std::filesystem;

    // Get PATHEXT extensions
    std::vector<std::string> pathext_list;
    if (auto* pathext = std::getenv("PATHEXT")) {
        std::string pe(pathext);
        size_t pos = 0;
        while (pos < pe.size()) {
            auto semi = pe.find(';', pos);
            auto ext = pe.substr(pos, semi - pos);
            if (!ext.empty()) pathext_list.push_back(ext);
            pos = (semi == std::string::npos) ? pe.size() : semi + 1;
        }
    } else {
        pathext_list = {".COM", ".EXE", ".BAT", ".CMD"};
    }

    // Get PATH directories
    std::vector<std::string> path_dirs;
    if (auto* path = std::getenv("PATH")) {
        std::string p(path);
        size_t pos = 0;
        while (pos < p.size()) {
            auto semi = p.find(';', pos);
            auto dir = p.substr(pos, semi - pos);
            if (!dir.empty()) path_dirs.push_back(dir);
            pos = (semi == std::string::npos) ? p.size() : semi + 1;
        }
    }

    std::string cmd_str(command);
    for (const auto& dir : path_dirs) {
        for (const auto& ext : pathext_list) {
            auto candidate = fs::path(dir) / (cmd_str + ext);
            if (fs::exists(candidate)) {
                return candidate;
            }
        }
    }

    return std::nullopt;
}
#endif

} // namespace openclaw::infra
