#include "openclaw/gateway/exec_approval_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_exec_approval_handlers(Protocol& protocol) {
    // exec.approval.request — request approval for a command execution.
    protocol.register_method("exec.approval.request",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto command = params.value("command", "");
            auto tool = params.value("tool", "");
            auto session_id = params.value("sessionId", "");
            if (command.empty() && tool.empty()) {
                co_return json{{"ok", false}, {"error", "command or tool is required"}};
            }
            // In a full implementation, this would queue the request and wait
            // for operator approval via the control UI.
            co_return json{
                {"ok", true},
                {"approvalId", ""},
                {"status", "auto_approved"},
                {"command", command},
            };
        },
        "Request approval for command execution", "exec");

    // exec.approval.resolve — approve or reject a pending approval.
    protocol.register_method("exec.approval.resolve",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto approval_id = params.value("approvalId", "");
            auto decision = params.value("decision", "");
            if (approval_id.empty() || decision.empty()) {
                co_return json{{"ok", false}, {"error", "approvalId and decision are required"}};
            }
            co_return json{{"ok", true}, {"approvalId", approval_id}, {"decision", decision}};
        },
        "Resolve a pending execution approval", "exec");

    // exec.approval.waitDecision — block until a decision is made.
    protocol.register_method("exec.approval.waitDecision",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto approval_id = params.value("approvalId", "");
            if (approval_id.empty()) {
                co_return json{{"ok", false}, {"error", "approvalId is required"}};
            }
            co_return json{
                {"ok", true},
                {"approvalId", approval_id},
                {"decision", "approved"},
            };
        },
        "Wait for execution approval decision", "exec");

    // exec.approvals.get — get current approval policy.
    protocol.register_method("exec.approvals.get",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"policy", "auto_approve"},
                {"allowedTools", json::array()},
                {"blockedTools", json::array()},
            };
        },
        "Get execution approval policy", "exec");

    // exec.approvals.set — update approval policy.
    protocol.register_method("exec.approvals.set",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto policy = params.value("policy", "auto_approve");
            co_return json{{"ok", true}, {"policy", policy}};
        },
        "Set execution approval policy", "exec");

    // exec.approvals.node.get — get per-node approval policy.
    protocol.register_method("exec.approvals.node.get",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto node_id = params.value("nodeId", "");
            if (node_id.empty()) {
                co_return json{{"ok", false}, {"error", "nodeId is required"}};
            }
            co_return json{
                {"ok", true},
                {"nodeId", node_id},
                {"policy", "auto_approve"},
            };
        },
        "Get per-node execution approval policy", "exec");

    // exec.approvals.node.set — update per-node approval policy.
    protocol.register_method("exec.approvals.node.set",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto node_id = params.value("nodeId", "");
            auto policy = params.value("policy", "auto_approve");
            if (node_id.empty()) {
                co_return json{{"ok", false}, {"error", "nodeId is required"}};
            }
            co_return json{{"ok", true}, {"nodeId", node_id}, {"policy", policy}};
        },
        "Set per-node execution approval policy", "exec");

    LOG_INFO("Registered exec approval handlers");
}

} // namespace openclaw::gateway
