#include "openclaw/infra/exec_trust.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sys/stat.h>

namespace openclaw::infra {

auto classify_risky_safe_bin_dir(std::string_view dir)
    -> std::optional<SafeBinDirRisk> {
    if (dir.empty()) return SafeBinDirRisk::Relative;

    // Not an absolute path
    if (dir[0] != '/') return SafeBinDirRisk::Relative;

    // Lowercase for case-insensitive matching
    std::string lower(dir);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Temporary directories — mutable and easy to poison
    if (lower == "/tmp" || lower.starts_with("/tmp/") ||
        lower == "/var/tmp" || lower.starts_with("/var/tmp/") ||
        lower == "/private/tmp" || lower.starts_with("/private/tmp/")) {
        return SafeBinDirRisk::Temporary;
    }

    // Package manager directories — often user-writable
    if (lower == "/usr/local/bin" || lower.starts_with("/usr/local/bin/") ||
        lower == "/opt/homebrew/bin" || lower.starts_with("/opt/homebrew/bin/") ||
        lower == "/opt/local/bin" || lower.starts_with("/opt/local/bin/") ||
        lower.find("linuxbrew") != std::string::npos) {
        return SafeBinDirRisk::PackageManager;
    }

    // Home-scoped paths — typically user-writable
    if (lower.starts_with("/users/") || lower.starts_with("/home/") ||
        lower.find("/.local/bin") != std::string::npos) {
        return SafeBinDirRisk::HomeScoped;
    }

    return std::nullopt;  // Safe
}

auto safe_bin_risk_description(SafeBinDirRisk risk) -> std::string {
    switch (risk) {
        case SafeBinDirRisk::Relative:
            return "Not an absolute path — cannot be trusted";
        case SafeBinDirRisk::Temporary:
            return "Temporary directory — mutable and easy to poison";
        case SafeBinDirRisk::PackageManager:
            return "Package manager directory — often user-writable";
        case SafeBinDirRisk::HomeScoped:
            return "Home-scoped path — typically user-writable";
    }
    return "Unknown risk";
}

auto is_world_writable(std::string_view path) -> bool {
    struct stat st{};
    if (stat(std::string(path).c_str(), &st) != 0) {
        return false;
    }
    return (st.st_mode & S_IWOTH) != 0;
}

} // namespace openclaw::infra
