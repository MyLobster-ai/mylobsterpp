#include "openclaw/gateway/plugin_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_plugin_handlers(Protocol& protocol,
                              plugins::PluginLoader& plugins) {
    // plugin.list
    protocol.register_method("plugin.list",
        [&plugins]([[maybe_unused]] json params) -> awaitable<json> {
            auto names = plugins.loaded_names();
            json result = json::array();
            for (auto name : names) {
                auto* p = plugins.get(name);
                result.push_back(json{
                    {"name", std::string(name)},
                    {"loaded", p != nullptr},
                });
            }
            co_return json{{"plugins", result}, {"count", result.size()}};
        },
        "List installed plugins", "plugin");

    // plugin.install
    protocol.register_method("plugin.install",
        [&plugins]([[maybe_unused]] json params) -> awaitable<json> {
            auto path = params.value("path", "");
            if (path.empty()) {
                co_return json{{"ok", false}, {"error", "path is required"}};
            }
            auto result = plugins.load(path);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto* p = result.value();
            co_return json{
                {"ok", true},
                {"name", std::string(p->name())},
            };
        },
        "Install a plugin from path or URL", "plugin");

    // plugin.uninstall
    protocol.register_method("plugin.uninstall",
        [&plugins]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            auto result = plugins.unload(name);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Uninstall a plugin", "plugin");

    // plugin.enable
    protocol.register_method("plugin.enable",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: plugin enable/disable state tracking.
            co_return json{{"ok", true}};
        },
        "Enable an installed plugin", "plugin");

    // plugin.disable
    protocol.register_method("plugin.disable",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}};
        },
        "Disable a plugin", "plugin");

    // plugin.configure
    protocol.register_method("plugin.configure",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: update plugin settings at runtime.
            co_return json{{"ok", true}};
        },
        "Update plugin settings", "plugin");

    // plugin.call
    protocol.register_method("plugin.call",
        [&plugins]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("plugin", "");
            auto function = params.value("function", "");
            if (name.empty() || function.empty()) {
                co_return json{{"ok", false}, {"error", "plugin and function are required"}};
            }
            auto* p = plugins.get(name);
            if (!p) {
                co_return json{{"ok", false}, {"error", "Plugin not found: " + name}};
            }
            // TODO: call exported plugin function.
            co_return json{{"ok", false}, {"error", "Plugin function calls not yet implemented"}};
        },
        "Call an exported plugin function", "plugin");

    // plugin.status
    protocol.register_method("plugin.status",
        [&plugins]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            auto* p = plugins.get(name);
            co_return json{
                {"ok", true},
                {"name", name},
                {"loaded", p != nullptr},
            };
        },
        "Get plugin runtime status", "plugin");

    LOG_INFO("Registered plugin handlers");
}

} // namespace openclaw::gateway
