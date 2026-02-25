#include "openclaw/infra/paths.hpp"
#include "openclaw/core/logger.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#endif

namespace openclaw::infra {

namespace fs = std::filesystem;

auto home_dir() -> fs::path {
#ifdef _WIN32
    // Try USERPROFILE first (standard on Windows)
    if (const auto* home = std::getenv("USERPROFILE"); home && *home) {
        return fs::path(home);
    }
    // Fall back to HOMEDRIVE + HOMEPATH
    const auto* drive = std::getenv("HOMEDRIVE");
    const auto* hpath = std::getenv("HOMEPATH");
    if (drive && hpath) {
        return fs::path(std::string(drive) + hpath);
    }
    return fs::path("C:\\Users\\Default");
#else
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
#endif
}

auto data_dir() -> fs::path {
#ifdef _WIN32
    if (const auto* appdata = std::getenv("LOCALAPPDATA"); appdata && *appdata) {
        return fs::path(appdata) / "openclaw" / "data";
    }
    return home_dir() / "AppData" / "Local" / "openclaw" / "data";
#elif defined(__APPLE__)
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
#ifdef _WIN32
    if (const auto* appdata = std::getenv("LOCALAPPDATA"); appdata && *appdata) {
        return fs::path(appdata) / "openclaw" / "config";
    }
    return home_dir() / "AppData" / "Local" / "openclaw" / "config";
#elif defined(__APPLE__)
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
#ifdef _WIN32
    if (const auto* appdata = std::getenv("LOCALAPPDATA"); appdata && *appdata) {
        return fs::path(appdata) / "openclaw" / "cache";
    }
    return home_dir() / "AppData" / "Local" / "openclaw" / "cache";
#elif defined(__APPLE__)
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
#ifdef _WIN32
    if (const auto* appdata = std::getenv("LOCALAPPDATA"); appdata && *appdata) {
        return fs::path(appdata) / "openclaw" / "logs";
    }
    return home_dir() / "AppData" / "Local" / "openclaw" / "logs";
#elif defined(__APPLE__)
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
#ifdef _WIN32
    // Windows has no runtime dir concept; use temp + PID
    return fs::temp_directory_path() / ("openclaw-" + std::to_string(::GetCurrentProcessId()));
#elif defined(__APPLE__)
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

// normalize_at_prefix() is defined in sandbox_paths.cpp (canonical location).
// paths.hpp re-exports the declaration for convenience.

} // namespace openclaw::infra
