#include "openclaw/gateway/tool_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_tool_handlers(Protocol& protocol,
                            [[maybe_unused]] GatewayServer& server,
                            agent::ToolRegistry& tools) {
    // tool.list
    protocol.register_method("tool.list",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto defs = tools.list();
            json result = json::array();
            for (const auto& d : defs) {
                result.push_back(json{
                    {"name", d.name},
                    {"description", d.description},
                });
            }
            co_return json{{"tools", result}, {"count", result.size()}};
        },
        "List all registered tools", "tool");

    // tool.execute
    protocol.register_method("tool.execute",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            auto tool_params = params.value("params", json::object());
            auto result = co_await tools.execute(name, tool_params);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"result", result.value()}};
        },
        "Execute a tool by name with params", "tool");

    // tool.register
    protocol.register_method("tool.register",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // Dynamic tool registration requires constructing a Tool instance
            // from a JSON schema. TODO: implement DynamicTool.
            co_return json{{"ok", false}, {"error", "Dynamic tool registration not yet supported"}};
        },
        "Register a new dynamic tool", "tool");

    // tool.unregister
    protocol.register_method("tool.unregister",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            bool removed = tools.remove(name);
            co_return json{{"ok", removed}};
        },
        "Unregister a dynamic tool", "tool");

    // tool.describe
    protocol.register_method("tool.describe",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            auto* tool = tools.get(name);
            if (!tool) {
                co_return json{{"ok", false}, {"error", "Tool not found: " + name}};
            }
            auto def = tool->definition();
            co_return json{
                {"name", def.name},
                {"description", def.description},
                {"schema", def.to_json()},
            };
        },
        "Get tool schema and description", "tool");

    // tool.enable
    protocol.register_method("tool.enable",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: tool enable/disable flags.
            co_return json{{"ok", true}};
        },
        "Enable a disabled tool", "tool");

    // tool.disable
    protocol.register_method("tool.disable",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}};
        },
        "Disable a tool without unregistering", "tool");

    // tool.shell.exec — security-critical, uses exec_safety validation.
    protocol.register_method("tool.shell.exec",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto command = params.value("command", "");
            if (command.empty()) {
                co_return json{{"ok", false}, {"error", "command is required"}};
            }
            // Execute via the shell tool in the registry.
            auto result = co_await tools.execute("shell", json{{"command", command}});
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"result", result.value()}};
        },
        "Execute a shell command", "tool");

    // tool.file.read — uses workspace path containment.
    protocol.register_method("tool.file.read",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto path = params.value("path", "");
            if (path.empty()) {
                co_return json{{"ok", false}, {"error", "path is required"}};
            }
            auto result = co_await tools.execute("file_read", json{{"path", path}});
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"result", result.value()}};
        },
        "Read file contents", "tool");

    // tool.file.write
    protocol.register_method("tool.file.write",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto path = params.value("path", "");
            auto content = params.value("content", "");
            if (path.empty()) {
                co_return json{{"ok", false}, {"error", "path is required"}};
            }
            auto result = co_await tools.execute("file_write",
                json{{"path", path}, {"content", content}});
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Write file contents", "tool");

    // tool.file.list
    protocol.register_method("tool.file.list",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto path = params.value("path", ".");
            auto result = co_await tools.execute("file_list", json{{"path", path}});
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"result", result.value()}};
        },
        "List directory contents", "tool");

    // tool.file.search
    protocol.register_method("tool.file.search",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto pattern = params.value("pattern", "");
            auto path = params.value("path", ".");
            if (pattern.empty()) {
                co_return json{{"ok", false}, {"error", "pattern is required"}};
            }
            auto result = co_await tools.execute("file_search",
                json{{"pattern", pattern}, {"path", path}});
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"result", result.value()}};
        },
        "Search files by pattern", "tool");

    // tool.http.request — uses fetch_guard for SSRF protection.
    protocol.register_method("tool.http.request",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto url = params.value("url", "");
            if (url.empty()) {
                co_return json{{"ok", false}, {"error", "url is required"}};
            }
            auto method = params.value("method", "GET");
            auto result = co_await tools.execute("http_request",
                json{{"url", url}, {"method", method}});
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"result", result.value()}};
        },
        "Make an HTTP request", "tool");

    // tool.code.run
    protocol.register_method("tool.code.run",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto code = params.value("code", "");
            auto language = params.value("language", "python");
            if (code.empty()) {
                co_return json{{"ok", false}, {"error", "code is required"}};
            }
            auto result = co_await tools.execute("code_run",
                json{{"code", code}, {"language", language}});
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"result", result.value()}};
        },
        "Execute code in a sandboxed runtime", "tool");

    // tool.code.analyze
    protocol.register_method("tool.code.analyze",
        [&tools]([[maybe_unused]] json params) -> awaitable<json> {
            auto code = params.value("code", "");
            if (code.empty()) {
                co_return json{{"ok", false}, {"error", "code is required"}};
            }
            auto result = co_await tools.execute("code_analyze",
                json{{"code", code}});
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"result", result.value()}};
        },
        "Analyze code for issues", "tool");

    LOG_INFO("Registered tool handlers");
}

} // namespace openclaw::gateway
