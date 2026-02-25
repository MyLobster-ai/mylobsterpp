#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openclaw::infra {

/// Default trusted directories for safe binary execution.
/// These are immutable system paths unlikely to be user-writable.
inline const std::vector<std::string> kDefaultTrustedDirs = {"/bin", "/usr/bin"};

/// Risk categories for safe-bin trusted directory classification.
enum class SafeBinDirRisk {
    Relative,       // Not an absolute path
    Temporary,      // /tmp, /var/tmp, /private/tmp — mutable and easy to poison
    PackageManager, // /usr/local/bin, /opt/homebrew/bin — often user-writable
    HomeScoped,     // ~/bin, /home/*/bin — typically user-writable
};

/// Classifies a safe-bin trusted directory for potential risks.
/// Returns nullopt if the directory is considered safe.
auto classify_risky_safe_bin_dir(std::string_view dir)
    -> std::optional<SafeBinDirRisk>;

/// Returns a human-readable risk description for a SafeBinDirRisk.
auto safe_bin_risk_description(SafeBinDirRisk risk) -> std::string;

/// Returns true if the given path is a world-writable directory.
auto is_world_writable(std::string_view path) -> bool;

} // namespace openclaw::infra
