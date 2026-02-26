#include "openclaw/gateway/agent_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_agent_handlers(Protocol& protocol,
                             GatewayServer& server,
                             sessions::SessionManager& sessions,
                             agent::AgentRuntime& runtime) {
    // agent.chat and agent.chat.stream are registered by register_chat_handlers().
    // agent.chat.cancel — cancel an in-flight run.
    protocol.register_method("agent.chat.cancel",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto run_id = params.value("runId", "");
            if (run_id.empty()) {
                co_return json{{"ok", false}, {"error", "runId is required"}};
            }
            // TODO: wire cancellation token to running completions.
            LOG_INFO("Cancel requested for run {}", run_id);
            co_return json{{"ok", true}, {"runId", run_id}};
        },
        "Cancel an in-progress agent response", "agent");

    // agent.system_prompt.get
    protocol.register_method("agent.system_prompt.get",
        [&runtime]([[maybe_unused]] json params) -> awaitable<json> {
            (void)runtime;
            std::string prompt;
            co_return json{{"system_prompt", prompt}};
        },
        "Get the current system prompt", "agent");

    // agent.system_prompt.set
    protocol.register_method("agent.system_prompt.set",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto prompt = params.value("system_prompt", "");
            // Store in runtime config for use by chat handlers.
            // TODO: persist to RuntimeConfig.
            co_return json{{"ok", true}};
        },
        "Set the system prompt", "agent");

    // agent.thinking.get
    protocol.register_method("agent.thinking.get",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // Default thinking mode.
            co_return json{{"mode", "none"}};
        },
        "Get current thinking mode", "agent");

    // agent.thinking.set
    protocol.register_method("agent.thinking.set",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto mode = params.value("mode", "none");
            // TODO: store thinking mode in AgentRuntime.
            co_return json{{"ok", true}, {"mode", mode}};
        },
        "Set thinking mode", "agent");

    // agent.model.get
    protocol.register_method("agent.model.get",
        [&runtime]([[maybe_unused]] json params) -> awaitable<json> {
            std::string model;
            if (auto provider = runtime.provider(); provider) {
                auto models = provider->models();
                if (!models.empty()) {
                    model = models[0];
                }
            }
            co_return json{{"model", model}};
        },
        "Get the active model", "agent");

    // agent.model.set
    protocol.register_method("agent.model.set",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto model = params.value("model", "");
            if (model.empty()) {
                co_return json{{"ok", false}, {"error", "model is required"}};
            }
            // TODO: switch provider model at runtime.
            co_return json{{"ok", true}, {"model", model}};
        },
        "Set the active model", "agent");

    // agent.conversation.create
    protocol.register_method("agent.conversation.create",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto user_id = params.value("userId", "default");
            auto device_id = params.value("deviceId", "default");
            auto result = co_await sessions.create_session(user_id, device_id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto& session = result.value();
            co_return json{
                {"ok", true},
                {"id", session.session.id},
                {"userId", session.session.user_id},
            };
        },
        "Create a new conversation", "agent");

    // agent.conversation.list
    protocol.register_method("agent.conversation.list",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto user_id = params.value("userId", "default");
            auto result = co_await sessions.list_sessions(user_id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            json conversations = json::array();
            for (const auto& s : result.value()) {
                conversations.push_back(json{
                    {"id", s.session.id},
                    {"userId", s.session.user_id},
                    {"state", s.state},
                    {"metadata", s.metadata},
                });
            }
            co_return json{{"conversations", conversations}};
        },
        "List conversations", "agent");

    // agent.conversation.get
    protocol.register_method("agent.conversation.get",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            auto result = co_await sessions.get_session(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto& s = result.value();
            co_return json{
                {"id", s.session.id},
                {"userId", s.session.user_id},
                {"state", s.state},
                {"metadata", s.metadata},
            };
        },
        "Get conversation details and messages", "agent");

    // agent.conversation.delete
    protocol.register_method("agent.conversation.delete",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            auto result = co_await sessions.end_session(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Delete a conversation", "agent");

    // agent.conversation.rename
    protocol.register_method("agent.conversation.rename",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            auto name = params.value("name", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            // TODO: update session metadata with new name.
            co_return json{{"ok", true}, {"id", id}, {"name", name}};
        },
        "Rename a conversation", "agent");

    LOG_INFO("Registered agent handlers");
}

} // namespace openclaw::gateway
