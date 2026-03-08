#include "openclaw/gateway/agents_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_agents_handlers(Protocol& protocol,
                              [[maybe_unused]] GatewayServer& server) {
    // agents.list — list configured agents.
    protocol.register_method("agents.list",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // In a full implementation, agents would be loaded from workspace config.
            co_return json{
                {"ok", true},
                {"agents", json::array({
                    json{
                        {"id", "default"},
                        {"name", "Default Agent"},
                        {"model", "claude-sonnet-4-6"},
                        {"active", true},
                    },
                })},
            };
        },
        "List configured agents", "agents");

    // agents.create — create a new agent configuration.
    protocol.register_method("agents.create",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            auto model = params.value("model", "claude-sonnet-4-6");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            LOG_INFO("Creating agent: name={}, model={}", name, model);
            co_return json{
                {"ok", true},
                {"id", name},
                {"name", name},
                {"model", model},
            };
        },
        "Create a new agent", "agents");

    // agents.update — update an existing agent's configuration.
    protocol.register_method("agents.update",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            auto name = params.value("name", "");
            auto model = params.value("model", "");
            co_return json{{"ok", true}, {"id", id}};
        },
        "Update an agent configuration", "agents");

    // agents.delete — delete an agent configuration.
    protocol.register_method("agents.delete",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            co_return json{{"ok", true}};
        },
        "Delete an agent", "agents");

    // agents.files.list — list files in an agent's workspace.
    protocol.register_method("agents.files.list",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto agent_id = params.value("agentId", "default");
            co_return json{
                {"ok", true},
                {"agentId", agent_id},
                {"files", json::array()},
            };
        },
        "List agent workspace files", "agents");

    // agents.files.get — read a file from an agent's workspace.
    protocol.register_method("agents.files.get",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto agent_id = params.value("agentId", "default");
            auto path = params.value("path", "");
            if (path.empty()) {
                co_return json{{"ok", false}, {"error", "path is required"}};
            }
            co_return json{
                {"ok", true},
                {"agentId", agent_id},
                {"path", path},
                {"content", ""},
            };
        },
        "Read agent workspace file", "agents");

    // agents.files.set — write a file to an agent's workspace.
    protocol.register_method("agents.files.set",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto agent_id = params.value("agentId", "default");
            auto path = params.value("path", "");
            auto content = params.value("content", "");
            if (path.empty()) {
                co_return json{{"ok", false}, {"error", "path is required"}};
            }
            co_return json{{"ok", true}, {"agentId", agent_id}, {"path", path}};
        },
        "Write agent workspace file", "agents");

    // agent.identity.get — get agent device identity info.
    protocol.register_method("agent.identity.get",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"deviceId", ""},
                {"publicKey", ""},
            };
        },
        "Get agent device identity", "agent");

    // agent.wait — wait for agent to be ready.
    protocol.register_method("agent.wait",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // Gateway is already running, so agent is ready.
            co_return json{{"ok", true}, {"ready", true}};
        },
        "Wait for agent to be ready", "agent");

    LOG_INFO("Registered agents handlers");
}

} // namespace openclaw::gateway
