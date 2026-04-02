#include "openclaw/gateway/tasks_handler.hpp"

#include <spdlog/spdlog.h>

namespace openclaw::gateway {

void register_tasks_handlers(Protocol& protocol,
                             tasks::TaskRegistry& registry) {
    using json = nlohmann::json;
    using boost::asio::awaitable;

    // v2026.3.31: tasks.list — list background tasks with filtering
    protocol.register_method(
        "tasks.list",
        [&registry]([[maybe_unused]] json params) -> awaitable<json> {
            tasks::TaskListParams list_params;
            list_params.limit = params.value("limit", 50);
            list_params.offset = params.value("offset", 0);
            if (params.contains("status")) {
                list_params.status_filter = params["status"].get<std::string>();
            }
            if (params.contains("agentId")) {
                list_params.agent_id = params["agentId"].get<std::string>();
            }
            if (params.contains("flowId")) {
                list_params.flow_id = params["flowId"].get<std::string>();
            }
            if (params.contains("origin")) {
                list_params.origin_filter = params["origin"].get<std::string>();
            }
            list_params.include_completed = params.value("includeCompleted", false);
            list_params.sort_by = params.value("sortBy", "created_at");
            list_params.sort_dir = params.value("sortDir", "desc");

            auto tasks = registry.list_tasks(list_params);

            json task_array = json::array();
            for (const auto& t : tasks) {
                task_array.push_back(t.to_json());
            }

            co_return json{
                {"ok", true},
                {"tasks", task_array},
                {"count", static_cast<int>(tasks.size())},
                {"activeCount", registry.active_count(
                    params.value("agentId", ""))},
            };
        },
        "List background tasks with filtering and paging",
        "tasks");

    // v2026.3.31: tasks.show — get a single task by ID
    protocol.register_method(
        "tasks.show",
        [&registry]([[maybe_unused]] json params) -> awaitable<json> {
            auto task_id = params.value("id", "");
            if (task_id.empty()) {
                co_return json{{"ok", false}, {"error", "missing task id"}};
            }

            auto result = registry.get_task(task_id);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }

            co_return json{{"ok", true}, {"task", result->to_json()}};
        },
        "Get a single background task by ID",
        "tasks");

    // v2026.3.31: tasks.cancel — cancel a running or pending task
    protocol.register_method(
        "tasks.cancel",
        [&registry]([[maybe_unused]] json params) -> awaitable<json> {
            auto task_id = params.value("id", "");
            if (task_id.empty()) {
                co_return json{{"ok", false}, {"error", "missing task id"}};
            }

            auto result = registry.cancel_task(task_id);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }

            co_return json{{"ok", true}};
        },
        "Cancel a running or pending background task",
        "tasks");

    // v2026.3.31: tasks.create — create a new task (internal use)
    protocol.register_method(
        "tasks.create",
        [&registry]([[maybe_unused]] json params) -> awaitable<json> {
            auto name = params.value("name", "");
            if (name.empty()) {
                co_return json{{"ok", false}, {"error", "missing task name"}};
            }

            auto origin = tasks::string_to_task_origin(
                params.value("origin", "manual"));

            std::optional<std::string_view> agent_id;
            std::string agent_id_str;
            if (params.contains("agentId")) {
                agent_id_str = params["agentId"].get<std::string>();
                agent_id = agent_id_str;
            }

            std::optional<std::string_view> session_key;
            std::string session_key_str;
            if (params.contains("sessionKey")) {
                session_key_str = params["sessionKey"].get<std::string>();
                session_key = session_key_str;
            }

            std::optional<std::string_view> flow_id;
            std::string flow_id_str;
            if (params.contains("flowId")) {
                flow_id_str = params["flowId"].get<std::string>();
                flow_id = flow_id_str;
            }

            auto result = registry.create_task(name, origin, agent_id,
                                                session_key, flow_id);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }

            co_return json{{"ok", true}, {"taskId", *result}};
        },
        "Create a new background task",
        "tasks");

    // v2026.3.31: tasks.flows.list — list task flows
    protocol.register_method(
        "tasks.flows.list",
        [&registry]([[maybe_unused]] json params) -> awaitable<json> {
            // List flows by querying tasks grouped by flow_id
            tasks::TaskListParams list_params;
            list_params.include_completed = params.value("includeCompleted", false);
            list_params.limit = params.value("limit", 50);
            list_params.offset = params.value("offset", 0);

            // For flows, we just return a summary
            co_return json{
                {"ok", true},
                {"totalTasks", static_cast<int>(registry.total_count())},
                {"activeTasks", registry.active_count()},
            };
        },
        "List task flows with summary",
        "tasks");

    // v2026.3.31: tasks.flows.show — get a single task flow
    protocol.register_method(
        "tasks.flows.show",
        [&registry]([[maybe_unused]] json params) -> awaitable<json> {
            auto flow_id = params.value("id", "");
            if (flow_id.empty()) {
                co_return json{{"ok", false}, {"error", "missing flow id"}};
            }

            auto result = registry.get_flow(flow_id);
            if (!result) {
                co_return json{{"ok", false}, {"error", result.error().what()}};
            }

            co_return json{{"ok", true}, {"flow", result->to_json()}};
        },
        "Get a single task flow by ID",
        "tasks");
}

} // namespace openclaw::gateway
