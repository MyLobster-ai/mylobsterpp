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

    // session.update
    protocol.register_method("session.update",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            // TODO: update session metadata via SessionManager.
            co_return json{{"ok", true}};
        },
        "Update session metadata", "session");

    // session.context.set
    protocol.register_method("session.context.set",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            auto key = params.value("key", "");
            auto value = params.value("value", json(nullptr));
            if (id.empty() || key.empty()) {
                co_return json{{"ok", false}, {"error", "id and key are required"}};
            }
            // TODO: store context variable in session.
            co_return json{{"ok", true}};
        },
        "Set session context variables", "session");

    // session.context.get
    protocol.register_method("session.context.get",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            auto key = params.value("key", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            // TODO: retrieve context variable from session.
            co_return json{{"ok", true}, {"value", json(nullptr)}};
        },
        "Get session context variables", "session");

    // session.context.clear
    protocol.register_method("session.context.clear",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            // TODO: clear all context variables for session.
            co_return json{{"ok", true}};
        },
        "Clear session context", "session");

    // session.history
    protocol.register_method("session.history",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto id = params.value("id", "");
            if (id.empty()) {
                co_return json{{"ok", false}, {"error", "id is required"}};
            }
            // TODO: retrieve message history from session store.
            co_return json{{"ok", true}, {"messages", json::array()}};
        },
        "Get session message history", "session");

    LOG_INFO("Registered session handlers");
}

} // namespace openclaw::gateway
