#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace openclaw::infra {

/// Parses a .env file and returns a map of key-value pairs.
/// Supports:
///   - KEY=VALUE
///   - KEY="VALUE" (double-quoted, with escape sequences)
///   - KEY='VALUE' (single-quoted, literal)
///   - # comments (full-line and inline after unquoted values)
///   - Empty lines are skipped
///   - export KEY=VALUE (optional export prefix)
auto parse(const std::filesystem::path& path)
    -> std::unordered_map<std::string, std::string>;

/// Parses a .env file and sets each key-value pair as an environment variable.
/// Existing environment variables are NOT overwritten unless `overwrite` is true.
void load(const std::filesystem::path& path, bool overwrite = false);

} // namespace openclaw::infra
