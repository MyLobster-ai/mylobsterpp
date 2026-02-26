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

} // namespace openclaw::infra
