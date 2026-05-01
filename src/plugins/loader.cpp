#include "openclaw/plugins/loader.hpp"
#include "openclaw/core/logger.hpp"

#include <algorithm>
#include <filesystem>

#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#error "PluginLoader: unsupported platform."
#endif

namespace openclaw::plugins {

namespace fs = std::filesystem;

namespace {

/// Returns true if the path has a platform-appropriate shared library extension.
auto is_shared_library(const fs::path& path) -> bool {
#if defined(__APPLE__)
    auto ext = path.extension().string();
    return ext == ".dylib" || ext == ".so";
#elif defined(_WIN32)
    return path.extension() == ".dll";
#else
    return path.extension() == ".so";
#endif
}

} // anonymous namespace

PluginLoader::~PluginLoader() {
    unload_all();
}

auto PluginLoader::load(const fs::path& path) -> Result<Plugin*> {
    if (!fs::exists(path)) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Plugin library not found",
            path.string()));
    }

    if (!is_shared_library(path)) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Not a shared library",
            path.string()));
    }

    LOG_INFO("Loading plugin from: {}", path.string());

#ifdef _WIN32
    // Load the DLL.
    HMODULE handle = ::LoadLibraryA(path.string().c_str());
    if (!handle) {
        return std::unexpected(make_error(
            ErrorCode::PluginError,
            "LoadLibrary failed",
            "GetLastError=" + std::to_string(::GetLastError())));
    }

    // Look up the factory function.
    void* sym = reinterpret_cast<void*>(::GetProcAddress(handle, kPluginFactorySymbol));
    if (!sym) {
        ::FreeLibrary(handle);
        return std::unexpected(make_error(
            ErrorCode::PluginError,
            "Factory symbol not found",
            std::string("Expected symbol '") + kPluginFactorySymbol + "' in " +
                path.filename().string()));
    }
#else
    // Open the shared library.
    // RTLD_NOW: resolve all symbols immediately (fail fast on missing deps).
    // RTLD_LOCAL: do not export symbols to other loaded libraries.
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* err = dlerror();
        return std::unexpected(make_error(
            ErrorCode::PluginError,
            "dlopen failed",
            err ? err : "unknown error"));
    }

    // Clear any existing error state.
    dlerror();

    // Look up the factory function.
    void* sym = dlsym(handle, kPluginFactorySymbol);
    const char* sym_err = dlerror();
    if (sym_err || !sym) {
        dlclose(handle);
        return std::unexpected(make_error(
            ErrorCode::PluginError,
            "Factory symbol not found",
            std::string("Expected symbol '") + kPluginFactorySymbol + "' in " +
                path.filename().string() +
                (sym_err ? std::string(": ") + sym_err : "")));
    }
#endif

    // Cast to the factory function pointer and invoke it.
    auto factory = reinterpret_cast<PluginFactory>(sym);
    std::unique_ptr<Plugin> plugin = factory();
    if (!plugin) {
#ifdef _WIN32
        ::FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return std::unexpected(make_error(
            ErrorCode::PluginError,
            "Plugin factory returned nullptr",
            path.filename().string()));
    }

    auto name = std::string(plugin->name());
    LOG_INFO("Loaded plugin '{}' v{} from {}",
             name, plugin->version(), path.filename().string());

    // If a plugin with the same name is already loaded, unload it first.
    if (loaded_.contains(name)) {
        LOG_WARN("Replacing already-loaded plugin '{}'", name);
        auto& existing = loaded_[name];
        existing.plugin.reset();  // destroy plugin before closing handle
        if (existing.handle) {
#ifdef _WIN32
            ::FreeLibrary(static_cast<HMODULE>(existing.handle));
#else
            dlclose(existing.handle);
#endif
        }
        loaded_.erase(name);
    }

    Plugin* raw_ptr = plugin.get();
    loaded_.emplace(name, LoadedPlugin{
        .handle = handle,
        .plugin = std::move(plugin),
        .path = path,
    });

    return raw_ptr;
}

auto PluginLoader::load_all(const fs::path& dir)
    -> Result<std::vector<Plugin*>>
{
    if (!fs::is_directory(dir)) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Plugin directory not found",
            dir.string()));
    }

    std::vector<Plugin*> plugins;
    std::vector<fs::path> candidates;

    // Collect all shared library files in the directory (non-recursive).
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && is_shared_library(entry.path())) {
            candidates.push_back(entry.path());
        }
    }

    // Sort for deterministic load order.
    std::sort(candidates.begin(), candidates.end());

    for (const auto& path : candidates) {
        auto result = load(path);
        if (result.has_value()) {
            plugins.push_back(result.value());
        } else {
            LOG_ERROR("Failed to load plugin '{}': {}",
                      path.filename().string(), result.error().what());
            // Continue loading other plugins; do not fail the batch.
        }
    }

    LOG_INFO("Loaded {} plugin(s) from {}", plugins.size(), dir.string());
    return plugins;
}

auto PluginLoader::unload(std::string_view name) -> Result<void> {
    auto it = loaded_.find(std::string(name));
    if (it == loaded_.end()) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Plugin not loaded",
            std::string(name)));
    }

    LOG_INFO("Unloading plugin '{}'", name);

    auto& entry = it->second;

    // Destroy the plugin instance first (calls ~Plugin before closing handle).
    entry.plugin.reset();

    // Close the shared library handle.
    if (entry.handle) {
#ifdef _WIN32
        ::FreeLibrary(static_cast<HMODULE>(entry.handle));
#else
        if (dlclose(entry.handle) != 0) {
            const char* err = dlerror();
            LOG_WARN("dlclose warning for '{}': {}", name,
                     err ? err : "unknown");
        }
#endif
    }

    loaded_.erase(it);
    return {};
}

auto PluginLoader::unload_all() -> void {
    // Collect names first to avoid iterator invalidation.
    std::vector<std::string> names;
    names.reserve(loaded_.size());
    for (const auto& [name, _] : loaded_) {
        names.push_back(name);
    }

    for (const auto& name : names) {
        auto result = unload(name);
        if (!result.has_value()) {
            LOG_WARN("Failed to unload plugin '{}': {}",
                     name, result.error().what());
        }
    }
}

auto PluginLoader::get(std::string_view name) const -> Plugin* {
    auto it = loaded_.find(std::string(name));
    if (it == loaded_.end()) {
        return nullptr;
    }
    return it->second.plugin.get();
}

auto PluginLoader::loaded_names() const -> std::vector<std::string_view> {
    std::vector<std::string_view> names;
    names.reserve(loaded_.size());
    for (const auto& [name, _] : loaded_) {
        names.push_back(name);
    }
    return names;
}

auto PluginLoader::size() const noexcept -> size_t {
    return loaded_.size();
}

// ---------------------------------------------------------------------------
// v2026.3.2: Ordered loading — bundled plugins before auto-discovered
// ---------------------------------------------------------------------------

auto PluginLoader::load_all_ordered(const fs::path& bundled_dir,
                                     const fs::path& auto_dir)
    -> Result<std::vector<Plugin*>>
{
    std::vector<Plugin*> all_plugins;

    // Phase 1: Load bundled plugins first (these take priority)
    if (fs::is_directory(bundled_dir)) {
        auto bundled_result = load_all(bundled_dir);
        if (bundled_result) {
            LOG_INFO("Loaded {} bundled plugin(s)", bundled_result->size());
            all_plugins.insert(all_plugins.end(),
                               bundled_result->begin(), bundled_result->end());
        } else {
            // v2026.3.2: Unknown plugin entries as warnings, not hard failures
            LOG_WARN("Failed to load bundled plugins from {}: {}",
                     bundled_dir.string(), bundled_result.error().what());
        }
    }

    // Phase 2: Load auto-discovered plugins (skip if already loaded by name)
    if (fs::is_directory(auto_dir)) {
        std::vector<fs::path> candidates;
        for (const auto& entry : fs::directory_iterator(auto_dir)) {
            if (entry.is_regular_file() && is_shared_library(entry.path())) {
                candidates.push_back(entry.path());
            }
        }
        std::sort(candidates.begin(), candidates.end());

        for (const auto& path : candidates) {
            auto result = load(path);
            if (result.has_value()) {
                all_plugins.push_back(result.value());
            } else {
                // v2026.3.2: Unknown/failed plugin entries as warnings
                LOG_WARN("Failed to auto-load plugin '{}': {}",
                         path.filename().string(), result.error().what());
            }
        }
    }

    LOG_INFO("Total loaded: {} plugin(s) (bundled + auto-discovered)",
             all_plugins.size());
    return all_plugins;
}

// ---------------------------------------------------------------------------
// v2026.3.2: Command name/description validation
// ---------------------------------------------------------------------------

auto PluginLoader::validate_command(std::string_view name,
                                     std::string_view description)
    -> Result<void>
{
    if (name.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Plugin command name must not be empty"));
    }

    // Command name must be alphanumeric + underscores/hyphens, 1-64 chars
    if (name.size() > 64) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Plugin command name too long (max 64 characters)",
            std::string(name)));
    }

    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '_' && c != '-' && c != '.') {
            return std::unexpected(make_error(
                ErrorCode::InvalidArgument,
                "Plugin command name contains invalid character",
                std::string(1, c) + " in " + std::string(name)));
        }
    }

    // Description validation
    if (description.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Plugin command description must not be empty",
            std::string(name)));
    }

    if (description.size() > 256) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Plugin command description too long (max 256 characters)",
            std::string(name)));
    }

    return {};
}

auto PluginLoader::set_enabled(std::string_view name, bool enabled) -> Result<void> {
    auto it = loaded_.find(std::string(name));
    if (it == loaded_.end()) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Plugin not loaded",
            std::string(name)));
    }
    it->second.enabled = enabled;
    LOG_INFO("Plugin '{}' {}", name, enabled ? "enabled" : "disabled");
    return {};
}

auto PluginLoader::is_enabled(std::string_view name) const -> Result<bool> {
    auto it = loaded_.find(std::string(name));
    if (it == loaded_.end()) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Plugin not loaded",
            std::string(name)));
    }
    return it->second.enabled;
}

auto PluginLoader::configure(std::string_view name, const nlohmann::json& settings)
    -> Result<void> {
    auto it = loaded_.find(std::string(name));
    if (it == loaded_.end()) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Plugin not loaded",
            std::string(name)));
    }
    auto result = it->second.plugin->configure(settings);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    it->second.settings = settings;
    LOG_INFO("Plugin '{}' reconfigured", name);
    return {};
}

auto PluginLoader::get_settings(std::string_view name) const
    -> Result<nlohmann::json> {
    auto it = loaded_.find(std::string(name));
    if (it == loaded_.end()) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Plugin not loaded",
            std::string(name)));
    }
    return it->second.settings;
}

} // namespace openclaw::plugins
