#include "openclaw/infra/paths.hpp"
#include "openclaw/core/logger.hpp"

#include <cstdlib>

#ifdef __APPLE__
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#endif

namespace openclaw::infra {

namespace fs = std::filesystem;

auto home_dir() -> fs::path {
    // Try HOME environment variable first
    if (const auto* home = std::getenv("HOME"); home && *home) {
        return fs::path(home);
    }

    // Fall back to passwd entry
#if defined(__APPLE__) || defined(__linux__)
    if (const auto* pw = ::getpwuid(::getuid()); pw && pw->pw_dir) {
        return fs::path(pw->pw_dir);
    }
#endif

    // Last resort
    return fs::path("/tmp");
}

auto data_dir() -> fs::path {
#ifdef __APPLE__
    return home_dir() / "Library" / "Application Support" / "openclaw";
#else
    // Linux / other Unix: XDG_DATA_HOME or ~/.local/share
    if (const auto* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return fs::path(xdg) / "openclaw";
    }
    return home_dir() / ".local" / "share" / "openclaw";
#endif
}

auto config_dir() -> fs::path {
#ifdef __APPLE__
    return home_dir() / "Library" / "Application Support" / "openclaw";
#else
    // Linux / other Unix: XDG_CONFIG_HOME or ~/.config
    if (const auto* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return fs::path(xdg) / "openclaw";
    }
    return home_dir() / ".config" / "openclaw";
#endif
}

auto cache_dir() -> fs::path {
#ifdef __APPLE__
    return home_dir() / "Library" / "Caches" / "openclaw";
#else
    // Linux / other Unix: XDG_CACHE_HOME or ~/.cache
    if (const auto* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
        return fs::path(xdg) / "openclaw";
    }
    return home_dir() / ".cache" / "openclaw";
#endif
}

auto logs_dir() -> fs::path {
#ifdef __APPLE__
    return home_dir() / "Library" / "Logs" / "openclaw";
#else
    // Linux / other Unix: XDG_STATE_HOME or ~/.local/state
    if (const auto* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg) {
        return fs::path(xdg) / "openclaw" / "logs";
    }
    return home_dir() / ".local" / "state" / "openclaw" / "logs";
#endif
}

auto runtime_dir() -> fs::path {
#ifdef __APPLE__
    return cache_dir() / "run";
#else
    // Linux / other Unix: XDG_RUNTIME_DIR or /tmp/openclaw-<uid>
    if (const auto* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg) {
        return fs::path(xdg) / "openclaw";
    }
    return fs::path("/tmp") / ("openclaw-" + std::to_string(::getuid()));
#endif
}

auto ensure_dir(const fs::path& path) -> fs::path {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        LOG_ERROR("Failed to create directory {}: {}", path.string(), ec.message());
    }
    auto canon = fs::canonical(path, ec);
    return ec ? path : canon;
}

} // namespace openclaw::infra
