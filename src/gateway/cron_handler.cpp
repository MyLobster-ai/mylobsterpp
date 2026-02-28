#include "openclaw/gateway/cron_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_cron_handlers(Protocol& protocol,
                            cron::CronScheduler& scheduler) {
    // cron.list
    protocol.register_method("cron.list",
        [&scheduler]([[maybe_unused]] json params) -> awaitable<json> {
            auto names = scheduler.task_names();
            json result = json::array();
            for (const auto& name : names) {
                result.push_back(json{{"name", name}});
            }
            co_return json{
                {"tasks", result},
                {"count", result.size()},
                {"running", scheduler.is_running()},
            };
        },
        "List scheduled tasks", "cron");

    // cron.create
    protocol.register_method("cron.create",
        [&scheduler]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            auto expression = params.value("expression", "");
            if (name.empty() || expression.empty()) {
                co_return json{{"ok", false}, {"error", "name and expression are required"}};
            }
            bool delete_after_run = params.value("deleteAfterRun", false);

            // v2026.2.26: Accept optional sessionKey for task context.
            auto session_key_raw = params.value("sessionKey", "");
            std::optional<std::string> session_key;
            if (!session_key_raw.empty()) {
                // Normalize to prevent double-prefixing (e.g. "agent:agent:key").
                auto agent_id = params.value("agentId", "");
                session_key = agent_id.empty()
                    ? session_key_raw
                    : utils::normalize_session_key(session_key_raw, agent_id);
            }

            // Create a placeholder task. Real tasks would execute agent actions.
            auto task_session_key = session_key;
            auto result = scheduler.schedule(name, expression,
                [name, task_session_key]() -> awaitable<void> {
                    if (task_session_key) {
                        LOG_INFO("Cron task '{}' executed (session={})",
                                 name, *task_session_key);
                    } else {
                        LOG_INFO("Cron task '{}' executed", name);
                    }
                    co_return;
                },
                delete_after_run);

            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }

            json response = {{"ok", true}, {"name", name}};
            if (session_key) {
                response["sessionKey"] = *session_key;
            }
            co_return response;
        },
        "Create a scheduled task", "cron");

    // cron.delete
    protocol.register_method("cron.delete",
        [&scheduler]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            auto result = scheduler.cancel(name);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Delete a scheduled task", "cron");

    // cron.enable
    protocol.register_method("cron.enable",
        []([[maybe_unused]] json params) -> awaitable<json> {
            // TODO: per-task enable/disable.
            co_return json{{"ok", true}};
        },
        "Enable a scheduled task", "cron");

    // cron.disable
    protocol.register_method("cron.disable",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{{"ok", true}};
        },
        "Disable a scheduled task", "cron");

    // cron.trigger
    protocol.register_method("cron.trigger",
        [&scheduler]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "name is required"}};
            }
            auto result = scheduler.manual_run(name);
            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}};
        },
        "Manually trigger a scheduled task", "cron");

    // cron.status
    protocol.register_method("cron.status",
        [&scheduler]([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"running", scheduler.is_running()},
                {"taskCount", scheduler.size()},
            };
        },
        "Get cron scheduler status", "cron");

    LOG_INFO("Registered cron handlers");
}

} // namespace openclaw::gateway
