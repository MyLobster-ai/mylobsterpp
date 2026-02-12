#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace openclaw::infra {

/// Returns the base data directory for OpenClaw.
/// Linux: $XDG_DATA_HOME/openclaw or ~/.local/share/openclaw
/// macOS: ~/Library/Application Support/openclaw
auto data_dir() -> std::filesystem::path;

/// Returns the configuration directory for OpenClaw.
/// Linux: $XDG_CONFIG_HOME/openclaw or ~/.config/openclaw
/// macOS: ~/Library/Application Support/openclaw
auto config_dir() -> std::filesystem::path;

/// Returns the cache directory for OpenClaw.
/// Linux: $XDG_CACHE_HOME/openclaw or ~/.cache/openclaw
/// macOS: ~/Library/Caches/openclaw
auto cache_dir() -> std::filesystem::path;

/// Returns the logs directory for OpenClaw.
/// Linux: $XDG_STATE_HOME/openclaw/logs or ~/.local/state/openclaw/logs
/// macOS: ~/Library/Logs/openclaw
auto logs_dir() -> std::filesystem::path;

/// Returns the runtime directory (for sockets, pid files, etc.).
/// Linux: $XDG_RUNTIME_DIR/openclaw or /tmp/openclaw-<uid>
/// macOS: ~/Library/Caches/openclaw/run
auto runtime_dir() -> std::filesystem::path;

/// Ensures a directory exists, creating it and parents if necessary.
/// Returns the resolved path on success.
auto ensure_dir(const std::filesystem::path& path) -> std::filesystem::path;

/// Returns the user's home directory.
auto home_dir() -> std::filesystem::path;

} // namespace openclaw::infra
