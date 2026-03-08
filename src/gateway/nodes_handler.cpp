#include "openclaw/gateway/nodes_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_nodes_handlers(Protocol& protocol,
                             [[maybe_unused]] GatewayServer& server) {
    // node.list — list connected companion nodes (macOS, iOS, Android).
    protocol.register_method("node.list",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // Return connected nodes. In a full implementation, the gateway
            // would track connected companion app WebSocket sessions.
            co_return json{{"ok", true}, {"nodes", json::array()}};
        },
        "List connected companion device nodes", "node");

    // node.describe — get detailed node capabilities.
    protocol.register_method("node.describe",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto node_id = params.value("nodeId", "");
            if (node_id.empty()) {
                co_return json{{"ok", false}, {"error", "nodeId is required"}};
            }
            co_return json{
                {"ok", true},
                {"nodeId", node_id},
                {"capabilities", json::array()},
                {"platform", "unknown"},
            };
        },
        "Get node capabilities and details", "node");

    // node.invoke — execute a command on a companion node.
    protocol.register_method("node.invoke",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto node_id = params.value("nodeId", "");
            auto command = params.value("command", "");
            if (node_id.empty() || command.empty()) {
                co_return json{{"ok", false}, {"error", "nodeId and command are required"}};
            }
            // Commands: camera.snap, camera.clip, location.get,
            // notifications.send, screen.record, sms.read, contacts.list, etc.
            LOG_INFO("Node invoke: {} -> {}", node_id, command);
            co_return json{
                {"ok", true},
                {"nodeId", node_id},
                {"command", command},
                {"status", "dispatched"},
            };
        },
        "Execute command on companion device", "node");

    // node.invoke.result — get result of a previous invocation.
    protocol.register_method("node.invoke.result",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto invocation_id = params.value("invocationId", "");
            if (invocation_id.empty()) {
                co_return json{{"ok", false}, {"error", "invocationId is required"}};
            }
            co_return json{
                {"ok", true},
                {"invocationId", invocation_id},
                {"status", "pending"},
                {"result", json(nullptr)},
            };
        },
        "Get result of node invocation", "node");

    // node.event — send event to a node.
    protocol.register_method("node.event",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto node_id = params.value("nodeId", "");
            auto event = params.value("event", "");
            if (node_id.empty() || event.empty()) {
                co_return json{{"ok", false}, {"error", "nodeId and event are required"}};
            }
            co_return json{{"ok", true}};
        },
        "Send event to companion node", "node");

    // node.rename — rename a connected node.
    protocol.register_method("node.rename",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto node_id = params.value("nodeId", "");
            auto name = params.value("name", "");
            if (node_id.empty() || name.empty()) {
                co_return json{{"ok", false}, {"error", "nodeId and name are required"}};
            }
            co_return json{{"ok", true}, {"nodeId", node_id}, {"name", name}};
        },
        "Rename a connected node", "node");

    // node.pair.request — request pairing with the gateway.
    protocol.register_method("node.pair.request",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto device_id = params.value("deviceId", "");
            auto device_name = params.value("deviceName", "");
            if (device_id.empty()) {
                co_return json{{"ok", false}, {"error", "deviceId is required"}};
            }
            co_return json{
                {"ok", true},
                {"deviceId", device_id},
                {"status", "pending_approval"},
            };
        },
        "Request device pairing", "node");

    // node.pair.list — list paired nodes.
    protocol.register_method("node.pair.list",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}, {"paired", json::array()}};
        },
        "List paired device nodes", "node");

    // node.pair.approve — approve a pairing request.
    protocol.register_method("node.pair.approve",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto device_id = params.value("deviceId", "");
            if (device_id.empty()) {
                co_return json{{"ok", false}, {"error", "deviceId is required"}};
            }
            co_return json{{"ok", true}, {"deviceId", device_id}};
        },
        "Approve device pairing request", "node");

    // node.pair.reject — reject a pairing request.
    protocol.register_method("node.pair.reject",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto device_id = params.value("deviceId", "");
            if (device_id.empty()) {
                co_return json{{"ok", false}, {"error", "deviceId is required"}};
            }
            co_return json{{"ok", true}, {"deviceId", device_id}};
        },
        "Reject device pairing request", "node");

    // node.pair.verify — verify a device pairing token.
    protocol.register_method("node.pair.verify",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto token = params.value("token", "");
            if (token.empty()) {
                co_return json{{"ok", false}, {"error", "token is required"}};
            }
            co_return json{{"ok", true}, {"valid", false}};
        },
        "Verify device pairing token", "node");

    // node.canvas.capability.refresh — refresh canvas capabilities for a node.
    protocol.register_method("node.canvas.capability.refresh",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto node_id = params.value("nodeId", "");
            if (node_id.empty()) {
                co_return json{{"ok", false}, {"error", "nodeId is required"}};
            }
            co_return json{{"ok", true}};
        },
        "Refresh canvas capabilities for node", "node");

    LOG_INFO("Registered nodes handlers");
}

} // namespace openclaw::gateway
