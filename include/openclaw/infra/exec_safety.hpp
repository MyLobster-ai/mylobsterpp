#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openclaw::infra {

/// Maximum shell wrapper unwrap depth to prevent infinite recursion.
static constexpr int kMaxUnwrapDepth = 10;

/// Known shell wrappers that execute their arguments as commands.
/// If argv[0] matches one of these, the "real" command starts at argv[1+].
inline auto is_shell_wrapper(std::string_view binary) -> bool {
    return binary == "sh" || binary == "bash" || binary == "zsh" ||
           binary == "dash" || binary == "env" || binary == "nice" ||
           binary == "nohup" || binary == "sudo" || binary == "doas" ||
           binary == "timeout";
}

/// Unwraps shell wrappers from an argv vector to find the actual command.
/// Returns the index of the first non-wrapper argument, or nullopt if the
/// unwrap depth cap is exceeded (fail-closed).
auto unwrap_shell_wrapper_argv(const std::vector<std::string>& argv)
    -> std::optional<size_t>;

/// For inline shell commands (e.g., `sh -c "rm -rf /"`), resolves
/// the index of the first token in the inline command string.
/// Returns nullopt if no inline command flag is found.
auto resolve_inline_command_token_index(const std::vector<std::string>& argv)
    -> std::optional<size_t>;

/// Returns true if the argv has trailing positional arguments after
/// the resolved command (which could be used for option injection).
auto has_trailing_positional_argv(const std::vector<std::string>& argv,
                                   size_t command_index) -> bool;

/// Validates that a system.run invocation is consistent and safe.
/// Checks: wrapper unwrap depth, inline command detection, and
/// ensures the resolved command is the same as the declared command.
auto validate_system_run_consistency(const std::vector<std::string>& argv,
                                      std::string_view declared_command)
    -> bool;

/// v2026.2.25: Hardens approved execution paths against TOCTOU attacks.
///
/// Validates:
///   1. `cwd` is not a symlink (prevents cwd-swap attacks)
///   2. `executable` canonicalizes to the same inode across lstat/stat/realpath
///   3. Inode identity is stable (no TOCTOU race between checks)
///
/// Returns true if the execution paths are safe, false otherwise.
auto harden_approved_execution_paths(const std::filesystem::path& cwd,
                                      const std::filesystem::path& executable)
    -> bool;

} // namespace openclaw::infra
