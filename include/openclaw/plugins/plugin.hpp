#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"

namespace openclaw::plugins {

using boost::asio::awaitable;

class PluginSDK;

/// Abstract base class for all dynamically loaded plugins.
///
/// A plugin is a shared library (.so / .dylib) that exports a factory function
/// named `openclaw_create_plugin`. The factory returns a `unique_ptr<Plugin>`
/// that the host will manage. Lifecycle:
///   1. Host calls `PluginFactory()` to create the plugin instance.
///   2. Host calls `init(sdk)` with a reference to the SDK context.
///   3. Plugin registers tools, channels, etc. via the SDK.
///   4. Host calls `shutdown()` before unloading.
class Plugin {
public:
    virtual ~Plugin() = default;

    /// Returns the plugin's human-readable name.
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    /// Returns the plugin's semantic version string (e.g. "1.2.3").
    [[nodiscard]] virtual auto version() const -> std::string_view = 0;

    /// Initialize the plugin with access to the host SDK.
    /// The plugin should register tools, channels, and any other
    /// resources it provides through the SDK reference.
    virtual auto init(PluginSDK& sdk) -> Result<void> = 0;

    /// Gracefully shut down the plugin, releasing any resources.
    virtual auto shutdown() -> awaitable<void> = 0;

    /// Apply a runtime settings update. Plugins that don't override this
    /// silently ignore the request. Default is a no-op so existing plugins
    /// remain ABI-compatible.
    virtual auto configure([[maybe_unused]] const nlohmann::json& settings)
        -> Result<void> {
        return {};
    }

    /// Call a named function exported by the plugin. Plugins that don't
    /// expose any callable functions return a NotFound error. Default
    /// implementation returns NotFound for ABI compatibility.
    virtual auto call(std::string_view function,
                      [[maybe_unused]] const nlohmann::json& args)
        -> awaitable<Result<nlohmann::json>> {
        co_return std::unexpected(make_error(
            ErrorCode::NotFound,
            "Plugin function not exported",
            std::string(function)));
    }
};

/// Factory function type that shared libraries must export.
/// The exported symbol must be named `openclaw_create_plugin`.
using PluginFactory = std::unique_ptr<Plugin>(*)();

/// Name of the exported factory symbol that the loader looks for.
inline constexpr const char* kPluginFactorySymbol = "openclaw_create_plugin";

} // namespace openclaw::plugins
