#include "openclaw/gateway/session_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_session_handlers(Protocol& protocol,
                               sessions::SessionManager& sessions) {
    // session.create
    protocol.register_method("session.create",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto user_id = params.value("userId", "default");
            auto device_id = params.value("deviceId", "default");
            auto channel = params.value("channel", "");

            Result<sessions::SessionData> result;
            if (channel.empty()) {
                result = co_await sessions.create_session(user_id, device_id);
            } else {
                result = co_await sessions.create_session(user_id, device_id, channel);
            }

            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{
                {"ok", true},
                {"session", result.value()},
            };
        },
        "Create a new user session", "session");

    // session.get
    protocol.register_method("session.get",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            auto result = co_await sessions.get_session(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"session", result.value()}};
        },
        "Get session details by id", "session");

    // session.list
    protocol.register_method("session.list",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto user_id = params.value("userId", "default");
            auto result = co_await sessions.list_sessions(user_id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            json sessions_json = json::array();
            for (const auto& s : result.value()) {
                sessions_json.push_back(s);
            }
            co_return json{{"ok", true}, {"sessions", sessions_json}};
        },
        "List active sessions", "session");

    // session.destroy
    protocol.register_method("session.destroy",
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
        "Destroy / end a session", "session");

    // session.heartbeat
    protocol.register_method("session.heartbeat",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            auto result = co_await sessions.touch_session(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Keep a session alive", "session");

    // session.update — update session metadata fields.
    protocol.register_method("session.update",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            auto result = co_await sessions.get_session(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            // Merge metadata updates
            auto& session_data = result.value();
            if (params.contains("metadata")) {
                for (auto& [key, val] : params["metadata"].items()) {
                    session_data.metadata[key] = val;
                }
            }
            if (params.contains("model")) {
                session_data.metadata["model"] = params["model"];
            }
            if (params.contains("thinkingLevel")) {
                session_data.metadata["thinkingLevel"] = params["thinkingLevel"];
            }
            co_return json{{"ok", true}, {"session", session_data}};
        },
        "Update session metadata", "session");

    // session.context.set — store a key-value context variable in session metadata.
    protocol.register_method("session.context.set",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            auto key = params.value("key", "");
            auto value = params.value("value", json(nullptr));
            if (id.empty() || key.empty()) {
                co_return json{{"ok", false}, {"error", "id and key are required"}};
            }
            auto result = co_await sessions.get_session(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto& session_data = result.value();
            if (!session_data.metadata.contains("context")) {
                session_data.metadata["context"] = json::object();
            }
            session_data.metadata["context"][key] = value;
            co_return json{{"ok", true}};
        },
        "Set session context variables", "session");

    // session.context.get — retrieve a context variable from session metadata.
    protocol.register_method("session.context.get",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            auto key = params.value("key", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            auto result = co_await sessions.get_session(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto& session_data = result.value();
            json value = json(nullptr);
            if (session_data.metadata.contains("context") &&
                session_data.metadata["context"].contains(key)) {
                value = session_data.metadata["context"][key];
            } else if (key.empty() && session_data.metadata.contains("context")) {
                value = session_data.metadata["context"];
            }
            co_return json{{"ok", true}, {"value", value}};
        },
        "Get session context variables", "session");

    // session.context.clear — clear all context variables for a session.
    protocol.register_method("session.context.clear",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            auto result = co_await sessions.get_session(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            auto& session_data = result.value();
            session_data.metadata["context"] = json::object();
            co_return json{{"ok", true}};
        },
        "Clear session context", "session");

    // session.history — retrieve message history from session.
    protocol.register_method("session.history",
        [&sessions]([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            auto result = co_await sessions.get_session(id);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            // Return metadata-tracked messages if available
            auto& session_data = result.value();
            json messages = json::array();
            if (session_data.metadata.contains("messages")) {
                messages = session_data.metadata["messages"];
            }
            co_return json{{"ok", true}, {"messages", messages}};
        },
        "Get session message history", "session");

    LOG_INFO("Registered session handlers");
}

} // namespace openclaw::gateway
