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

} // namespace openclaw::plugins
