#include "openclaw/core/secrets.hpp"
#include "openclaw/core/logger.hpp"

#include <array>
#include <cstdio>
#include <fstream>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace openclaw {

SecretResolver::SecretResolver(SecretsConfig config)
    : config_(std::move(config)) {}

auto SecretResolver::resolve(const SecretRef& ref) -> Result<std::string> {
    if (ref.source == "env") {
        return resolve_env(ref.id);
    }
    if (ref.source == "file") {
        return resolve_file(ref.id);
    }
    if (ref.source == "exec") {
        std::vector<std::string> args;
        // The ref.id is the command; args can come from the config
        if (config_.exec) {
            args = config_.exec->args;
        }
        return resolve_exec(ref.id, args);
    }
    return std::unexpected(make_error(
        ErrorCode::InvalidArgument,
        "Unknown secret source",
        ref.source));
}

auto SecretResolver::resolve_env(std::string_view key) -> Result<std::string> {
    if (key.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Empty environment variable name"));
    }

    // Check allowlist if configured
    if (config_.env && !config_.env->allowlist.empty()) {
        bool allowed = false;
        for (const auto& k : config_.env->allowlist) {
            if (k == key) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            return std::unexpected(make_error(
                ErrorCode::Forbidden,
                "Environment variable not in allowlist",
                std::string(key)));
        }
    }

    std::string key_str(key);
    auto* val = std::getenv(key_str.c_str());
    if (!val) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Environment variable not set",
            key_str));
    }

    return std::string(val);
}

auto SecretResolver::resolve_file(std::string_view path) -> Result<std::string> {
    if (path.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Empty file path for secret resolution"));
    }

    std::string path_str(path);

#ifndef _WIN32
    // Check file ownership and permissions
    struct stat st{};
    if (::stat(path_str.c_str(), &st) != 0) {
        return std::unexpected(make_error(
            ErrorCode::IoError,
            "Cannot stat secret file",
            path_str + ": " + std::string(strerror(errno))));
    }

    // Verify ownership (must be owned by current user or root)
    if (st.st_uid != ::getuid() && st.st_uid != 0) {
        return std::unexpected(make_error(
            ErrorCode::Forbidden,
            "Secret file not owned by current user or root",
            path_str));
    }

    // Verify permissions <= 0644 (no group/other write, no world read ideally)
    mode_t perm = st.st_mode & 0777;
    if (perm & 0133) {  // reject if group/other have write or execute
        LOG_WARN("Secret file {} has overly permissive permissions: {:o}", path_str, perm);
        return std::unexpected(make_error(
            ErrorCode::Forbidden,
            "Secret file permissions too permissive (must be <= 0644)",
            path_str + " has " + std::to_string(perm)));
    }
#endif

    // Read the file
    int max_bytes = 65536;
    if (config_.file) {
        max_bytes = config_.file->max_bytes;
    }

    std::ifstream file(path_str, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(make_error(
            ErrorCode::IoError,
            "Cannot open secret file",
            path_str));
    }

    std::string content;
    content.resize(max_bytes);
    file.read(content.data(), max_bytes);
    content.resize(static_cast<size_t>(file.gcount()));

    // Trim trailing newline (common in secret files)
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) {
        content.pop_back();
    }

    return content;
}

auto SecretResolver::resolve_exec(std::string_view cmd, const std::vector<std::string>& args)
    -> Result<std::string>
{
    if (cmd.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Empty command for exec secret resolution"));
    }

    // Build the command string
    std::string full_cmd(cmd);
    for (const auto& arg : args) {
        full_cmd += " ";
        // Basic quoting for safety
        full_cmd += "'" + arg + "'";
    }

    int max_output = 65536;
    if (config_.exec) {
        max_output = config_.exec->max_output_bytes;
    }

    // Execute with popen
    auto* pipe = ::popen(full_cmd.c_str(), "r");
    if (!pipe) {
        return std::unexpected(make_error(
            ErrorCode::IoError,
            "Failed to execute secret command",
            full_cmd));
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (static_cast<int>(output.size()) < max_output) {
        auto n = std::fread(buffer.data(), 1, buffer.size(), pipe);
        if (n == 0) break;
        output.append(buffer.data(), n);
    }

    int status = ::pclose(pipe);
    if (status != 0) {
        return std::unexpected(make_error(
            ErrorCode::IoError,
            "Secret command exited with non-zero status",
            full_cmd + " (status " + std::to_string(status) + ")"));
    }

    // Trim trailing newline
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    return output;
}

} // namespace openclaw
