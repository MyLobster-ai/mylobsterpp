#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "openclaw/core/error.hpp"
#include "openclaw/plugins/plugin.hpp"

namespace openclaw::plugins {

/// Dynamically loads plugin shared libraries using dlopen/dlsym (Unix)
/// or LoadLibrary/GetProcAddress (Windows, future).
///
/// Loaded plugins are tracked by name. The loader takes ownership of both
/// the dlopen handle and the plugin instance, ensuring proper unload order
/// (plugin destroyed before dlclose).
class PluginLoader {
public:
    PluginLoader() = default;
    ~PluginLoader();

    // Non-copyable, non-movable (owns dlopen handles).
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;
    PluginLoader(PluginLoader&&) = delete;
    PluginLoader& operator=(PluginLoader&&) = delete;

    /// Load a single plugin from a shared library file.
    /// @param path  Path to the .so / .dylib file.
    /// @returns     Non-owning pointer to the loaded plugin on success.
    auto load(const std::filesystem::path& path) -> Result<Plugin*>;

    /// Load all plugins from shared libraries found in a directory.
    /// Files must have the platform-appropriate extension (.so / .dylib).
    /// @param dir  Directory to scan for plugin libraries.
    /// @returns    Vector of non-owning pointers to loaded plugins.
    auto load_all(const std::filesystem::path& dir)
        -> Result<std::vector<Plugin*>>;

    /// Unload a plugin by name, destroying the instance and closing the library.
    auto unload(std::string_view name) -> Result<void>;

    /// Unload all plugins.
    auto unload_all() -> void;

    /// Returns a non-owning pointer to a loaded plugin by name, or nullptr.
    [[nodiscard]] auto get(std::string_view name) const -> Plugin*;

    /// Returns the names of all currently loaded plugins.
    [[nodiscard]] auto loaded_names() const -> std::vector<std::string_view>;

    /// Returns the number of loaded plugins.
    [[nodiscard]] auto size() const noexcept -> size_t;

    /// v2026.3.2: Load bundled plugins before auto-discovered plugins.
    /// @param bundled_dir  Directory containing bundled plugins (loaded first).
    /// @param auto_dir     Directory containing auto-discovered plugins (loaded second).
    auto load_all_ordered(const std::filesystem::path& bundled_dir,
                          const std::filesystem::path& auto_dir)
        -> Result<std::vector<Plugin*>>;

    /// v2026.3.2: Validate a plugin command registration.
    /// Returns error if name/description are invalid.
    static auto validate_command(std::string_view name, std::string_view description)
        -> Result<void>;

private:
    struct LoadedPlugin {
        void* handle = nullptr;
        std::unique_ptr<Plugin> plugin;
        std::filesystem::path path;
    };

    std::unordered_map<std::string, LoadedPlugin> loaded_;
};

} // namespace openclaw::plugins
