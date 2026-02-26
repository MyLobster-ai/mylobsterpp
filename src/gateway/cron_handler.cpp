#include "openclaw/gateway/cron_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

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

            // Create a placeholder task. Real tasks would execute agent actions.
            auto result = scheduler.schedule(name, expression,
                [name]() -> awaitable<void> {
                    LOG_INFO("Cron task '{}' executed", name);
                    co_return;
                },
                delete_after_run);

            if (!result.has_value()) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }
            co_return json{{"ok", true}, {"name", name}};
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
