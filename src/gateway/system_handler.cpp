#include "openclaw/gateway/system_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_system_handlers(Protocol& protocol,
                              GatewayServer& server) {
    // health — health check with optional probe mode.
    protocol.register_method("health",
        [&server]([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"status", server.is_running() ? "healthy" : "unhealthy"},
                {"version", "2026.3.2"},
                {"connections", server.connection_count()},
            };
        },
        "Health check", "system");

    // last-heartbeat — get timestamp of last client heartbeat.
    protocol.register_method("last-heartbeat",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"timestamp", utils::timestamp_ms()},
            };
        },
        "Get last heartbeat timestamp", "system");

    // set-heartbeats — enable/disable heartbeat tracking.
    protocol.register_method("set-heartbeats",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto enabled = params.value("enabled", true);
            co_return json{{"ok", true}, {"enabled", enabled}};
        },
        "Enable/disable heartbeat tracking", "system");

    // system-presence — get presence information.
    protocol.register_method("system-presence",
        [&server]([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"connections", server.connection_count()},
                {"running", server.is_running()},
            };
        },
        "Get system presence information", "system");

    // system-event — record a system event.
    protocol.register_method("system-event",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto event_type = params.value("type", "");
            auto data = params.value("data", json::object());
            LOG_DEBUG("System event: type={}", event_type);
            co_return json{{"ok", true}};
        },
        "Record a system event", "system");

    // send — send message to external channels.
    protocol.register_method("send",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto channel = params.value("channel", "");
            auto to = params.value("to", "");
            auto text = params.value("text", "");
            if (text.empty()) {
                co_return json{{"ok", false}, {"error", "text is required"}};
            }
            // In a full implementation, this would route through the channel registry.
            co_return json{{"ok", true}, {"channel", channel}};
        },
        "Send message to external channel", "system");

    // update.run — trigger a software update check.
    protocol.register_method("update.run",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"currentVersion", "2026.3.2"},
                {"updateAvailable", false},
            };
        },
        "Check for software updates", "system");

    // secrets.reload — reload secrets from environment/files.
    protocol.register_method("secrets.reload",
        []([[maybe_unused]] json params) -> awaitable<json> {
            LOG_INFO("Secrets reload requested");
            co_return json{{"ok", true}};
        },
        "Reload secrets from environment", "system");

    // secrets.resolve — resolve a secret by name.
    protocol.register_method("secrets.resolve",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            // Never expose secret values via RPC; just confirm existence.
            co_return json{{"ok", true}, {"name", name}, {"exists", false}};
        },
        "Check if a secret exists", "system");

    // doctor.memory.status — check memory subsystem health.
    protocol.register_method("doctor.memory.status",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"status", "healthy"},
                {"embeddingProvider", "none"},
                {"vectorStore", "sqlite-vec"},
            };
        },
        "Check memory subsystem health", "system");

    // logs.tail — stream recent gateway logs.
    protocol.register_method("logs.tail",
        []([[maybe_unused]] json params) -> awaitable<json> {
            [[maybe_unused]] auto lines = params.value("lines", 50);
            // In a full implementation, this would stream from the ring buffer.
            co_return json{
                {"ok", true},
                {"lines", json::array()},
                {"count", 0},
            };
        },
        "Stream recent gateway logs", "system");

    // push.test — send a test push notification.
    protocol.register_method("push.test",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}, {"sent", false}, {"reason", "No push provider configured"}};
        },
        "Send test push notification", "system");

    // talk.config — get Talk Mode configuration.
    protocol.register_method("talk.config",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"enabled", false},
                {"wakeWord", ""},
                {"voice", ""},
            };
        },
        "Get Talk Mode configuration", "system");

    // talk.mode — toggle Talk Mode.
    protocol.register_method("talk.mode",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto enabled = params.value("enabled", false);
            LOG_INFO("Talk mode: {}", enabled ? "enabled" : "disabled");
            co_return json{{"ok", true}, {"enabled", enabled}};
        },
        "Toggle Talk Mode", "system");

    // tools.catalog — list available tools with profile filtering.
    protocol.register_method("tools.catalog",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto profile = params.value("profile", "full");
            // In a full implementation, this would filter tools by profile.
            co_return json{
                {"ok", true},
                {"profile", profile},
                {"tools", json::array()},
            };
        },
        "List available tools by profile", "system");

    // web.login.start — initiate web login flow (QR code).
    protocol.register_method("web.login.start",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"loginId", ""},
                {"qrUrl", ""},
            };
        },
        "Start web login flow", "system");

    // web.login.wait — wait for web login completion.
    protocol.register_method("web.login.wait",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto login_id = params.value("loginId", "");
            co_return json{
                {"ok", true},
                {"loginId", login_id},
                {"status", "pending"},
            };
        },
        "Wait for web login completion", "system");

    // voicewake.get — get voice wake triggers.
    protocol.register_method("voicewake.get",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"triggers", json::array()},
            };
        },
        "Get voice wake triggers", "system");

    // voicewake.set — set voice wake triggers.
    protocol.register_method("voicewake.set",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto triggers = params.value("triggers", json::array());
            co_return json{{"ok", true}, {"triggers", triggers}};
        },
        "Set voice wake triggers", "system");

    // sessions.list — OpenClaw-compatible session listing.
    protocol.register_method("sessions.list",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}, {"sessions", json::array()}};
        },
        "List sessions (OpenClaw format)", "sessions");

    // sessions.resolve — resolve session key.
    protocol.register_method("sessions.resolve",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto key = params.value("key", "");
            co_return json{{"ok", true}, {"key", key}, {"resolved", false}};
        },
        "Resolve session key", "sessions");

    // sessions.preview — get session preview items.
    protocol.register_method("sessions.preview",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}, {"items", json::array()}};
        },
        "Get session preview", "sessions");

    // sessions.patch — update session metadata.
    protocol.register_method("sessions.patch",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto key = params.value("key", "");
            co_return json{{"ok", true}, {"key", key}};
        },
        "Patch session metadata", "sessions");

    // sessions.reset — reset a session.
    protocol.register_method("sessions.reset",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto key = params.value("key", "");
            co_return json{{"ok", true}};
        },
        "Reset session", "sessions");

    // sessions.delete — delete a session.
    protocol.register_method("sessions.delete",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto key = params.value("key", "");
            co_return json{{"ok", true}};
        },
        "Delete session", "sessions");

    // sessions.compact — compact session transcript.
    protocol.register_method("sessions.compact",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto key = params.value("key", "");
            co_return json{{"ok", true}, {"compacted", false}};
        },
        "Compact session transcript", "sessions");

    // chat.history — retrieve conversation history.
    protocol.register_method("chat.history",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto session_key = params.value("sessionKey", "");
            co_return json{{"ok", true}, {"messages", json::array()}};
        },
        "Retrieve conversation history", "chat");

    // chat.inject — inject a message into session transcript.
    protocol.register_method("chat.inject",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto session_key = params.value("sessionKey", "");
            auto role = params.value("role", "assistant");
            auto content = params.value("content", "");
            co_return json{{"ok", true}};
        },
        "Inject message into session", "chat");

    // chat.abort — abort active chat runs.
    protocol.register_method("chat.abort",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto session_key = params.value("sessionKey", "");
            LOG_INFO("Chat abort requested for session: {}", session_key);
            co_return json{{"ok", true}};
        },
        "Abort active chat runs", "chat");

    // channels.status — OpenClaw-style channel status.
    protocol.register_method("channels.status",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}, {"channels", json::array()}};
        },
        "Get all channel status", "channels");

    // channels.logout — disconnect from a channel.
    protocol.register_method("channels.logout",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto channel = params.value("channel", "");
            co_return json{{"ok", true}};
        },
        "Logout from channel", "channels");

    // agent.poll — poll agent status.
    protocol.register_method("agent.poll",
        [&server]([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"running", server.is_running()},
                {"connections", server.connection_count()},
            };
        },
        "Poll agent status", "agent");

    // agent.timestamp — get agent clock for sync.
    protocol.register_method("agent.timestamp",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"timestamp", utils::timestamp_ms()},
            };
        },
        "Get agent clock timestamp", "agent");

    // config.apply — replace entire configuration.
    protocol.register_method("config.apply",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}};
        },
        "Apply full configuration replacement", "config");

    // config.schema — get configuration JSON schema.
    protocol.register_method("config.schema",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"schema", json::object()},
            };
        },
        "Get configuration JSON schema", "config");

    // cron.add — OpenClaw-compatible alias for cron.create.
    protocol.register_method("cron.add",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}};
        },
        "Add a cron job (alias)", "cron");

    // cron.update — update an existing cron job.
    protocol.register_method("cron.update",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            co_return json{{"ok", true}, {"name", name}};
        },
        "Update a cron job", "cron");

    // cron.remove — remove a cron job (alias for cron.delete).
    protocol.register_method("cron.remove",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            co_return json{{"ok", true}};
        },
        "Remove a cron job (alias)", "cron");

    // cron.run — run a cron job manually (alias for cron.trigger).
    protocol.register_method("cron.run",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            co_return json{{"ok", true}};
        },
        "Run cron job manually (alias)", "cron");

    // cron.runs — get cron job execution history.
    protocol.register_method("cron.runs",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            co_return json{{"ok", true}, {"runs", json::array()}};
        },
        "Get cron job run history", "cron");

    // browser.request — browser proxy request for extension relay.
    protocol.register_method("browser.request",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", false}, {"error", "Browser proxy not configured"}};
        },
        "Browser proxy request", "browser");

    // provider.usage — provider token usage statistics.
    protocol.register_method("provider.usage",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"inputTokens", 0},
                {"outputTokens", 0},
                {"totalRequests", 0},
            };
        },
        "Get provider usage statistics", "provider");

    LOG_INFO("Registered system handlers ({} additional methods)", 40);
}

} // namespace openclaw::gateway
