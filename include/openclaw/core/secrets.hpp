#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"

// std::optional serializer for nlohmann/json — needed for SecretsConfig fields.
// Also defined in config.hpp; both are guarded by the template specialization mechanism.
namespace nlohmann {
template <typename T>
struct adl_serializer<std::optional<T>> {
    static void to_json(json& j, const std::optional<T>& opt) {
        if (opt.has_value()) {
            j = *opt;
        } else {
            j = nullptr;
        }
    }

    static void from_json(const json& j, std::optional<T>& opt) {
        if (j.is_null()) {
            opt = std::nullopt;
        } else {
            opt = j.get<T>();
        }
    }
};
} // namespace nlohmann

namespace openclaw {

/// A reference to a secret stored in an external provider.
struct SecretRef {
    std::string source;    // "env", "file", "exec"
    std::string provider;  // provider identifier
    std::string id;        // key/path/command identifier
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SecretRef, source, provider, id)

/// Sub-providers for the secrets management subsystem.
struct SecretsEnvProvider {
    std::vector<std::string> allowlist;  // if non-empty, only these env vars are allowed
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SecretsEnvProvider, allowlist)

struct SecretsFileProvider {
    std::string path;
    int timeout_ms = 5000;
    int max_bytes = 65536;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SecretsFileProvider, path, timeout_ms, max_bytes)

struct SecretsExecProvider {
    std::string command;
    std::vector<std::string> args;
    int timeout_ms = 5000;
    int max_output_bytes = 65536;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SecretsExecProvider, command, args, timeout_ms, max_output_bytes)

/// Configuration for the secrets management subsystem.
struct SecretsConfig {
    using EnvProvider = SecretsEnvProvider;
    using FileProvider = SecretsFileProvider;
    using ExecProvider = SecretsExecProvider;

    std::optional<EnvProvider> env;
    std::optional<FileProvider> file;
    std::optional<ExecProvider> exec;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SecretsConfig, env, file, exec)

/// Resolves secrets from various providers (env vars, files, exec).
class SecretResolver {
public:
    explicit SecretResolver(SecretsConfig config = {});

    /// Resolve a secret reference to its string value.
    auto resolve(const SecretRef& ref) -> Result<std::string>;

    /// Resolve from environment variable.
    auto resolve_env(std::string_view key) -> Result<std::string>;

    /// Resolve from a file (checks ownership and permissions <= 0644).
    auto resolve_file(std::string_view path) -> Result<std::string>;

    /// Resolve by executing a command (with timeout).
    auto resolve_exec(std::string_view cmd, const std::vector<std::string>& args) -> Result<std::string>;

private:
    SecretsConfig config_;
};

} // namespace openclaw
