#include "openclaw/infra/exec_safety.hpp"

#include <filesystem>

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

} // namespace openclaw::infra
