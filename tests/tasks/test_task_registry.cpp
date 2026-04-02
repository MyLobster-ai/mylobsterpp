#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/tasks/task_registry.hpp"

using namespace openclaw::tasks;
using json = nlohmann::json;

// --- Enum conversion tests ---

TEST_CASE("TaskStatus enum conversions", "[tasks]") {
    SECTION("to_string covers all values") {
        CHECK(task_status_to_string(TaskStatus::Pending) == "pending");
        CHECK(task_status_to_string(TaskStatus::Running) == "running");
        CHECK(task_status_to_string(TaskStatus::Completed) == "completed");
        CHECK(task_status_to_string(TaskStatus::Failed) == "failed");
        CHECK(task_status_to_string(TaskStatus::Cancelled) == "cancelled");
        CHECK(task_status_to_string(TaskStatus::Blocked) == "blocked");
        CHECK(task_status_to_string(TaskStatus::Lost) == "lost");
    }

    SECTION("from_string round-trips") {
        CHECK(string_to_task_status("pending") == TaskStatus::Pending);
        CHECK(string_to_task_status("running") == TaskStatus::Running);
        CHECK(string_to_task_status("completed") == TaskStatus::Completed);
        CHECK(string_to_task_status("failed") == TaskStatus::Failed);
        CHECK(string_to_task_status("cancelled") == TaskStatus::Cancelled);
        CHECK(string_to_task_status("blocked") == TaskStatus::Blocked);
        CHECK(string_to_task_status("lost") == TaskStatus::Lost);
    }

    SECTION("unknown string defaults to Pending") {
        CHECK(string_to_task_status("bogus") == TaskStatus::Pending);
        CHECK(string_to_task_status("") == TaskStatus::Pending);
    }
}

TEST_CASE("TaskOrigin enum conversions", "[tasks]") {
    SECTION("to_string covers all values") {
        CHECK(task_origin_to_string(TaskOrigin::Cron) == "cron");
        CHECK(task_origin_to_string(TaskOrigin::Subagent) == "subagent");
        CHECK(task_origin_to_string(TaskOrigin::Acp) == "acp");
        CHECK(task_origin_to_string(TaskOrigin::Background) == "background");
        CHECK(task_origin_to_string(TaskOrigin::Manual) == "manual");
    }

    SECTION("from_string round-trips") {
        CHECK(string_to_task_origin("cron") == TaskOrigin::Cron);
        CHECK(string_to_task_origin("subagent") == TaskOrigin::Subagent);
        CHECK(string_to_task_origin("acp") == TaskOrigin::Acp);
        CHECK(string_to_task_origin("background") == TaskOrigin::Background);
        CHECK(string_to_task_origin("manual") == TaskOrigin::Manual);
    }
}

// --- TaskRecord JSON tests ---

TEST_CASE("TaskRecord JSON serialization", "[tasks]") {
    TaskRecord record;
    record.id = "task_abc123";
    record.name = "test-task";
    record.status = TaskStatus::Running;
    record.origin = TaskOrigin::Cron;
    record.agent_id = "agent-1";
    record.session_key = "session-key";
    record.flow_id = "flow-1";
    record.created_at = 1700000000000;
    record.updated_at = 1700000001000;
    record.started_at = 1700000001000;

    SECTION("to_json preserves all fields") {
        auto j = record.to_json();
        CHECK(j["id"] == "task_abc123");
        CHECK(j["name"] == "test-task");
        CHECK(j["status"] == "running");
        CHECK(j["origin"] == "cron");
        CHECK(j["agentId"] == "agent-1");
        CHECK(j["sessionKey"] == "session-key");
        CHECK(j["flowId"] == "flow-1");
        CHECK(j["createdAt"] == 1700000000000);
        CHECK(j["updatedAt"] == 1700000001000);
        CHECK(j["startedAt"] == 1700000001000);
        CHECK(j["completedAt"] == 0);
    }

    SECTION("from_json round-trips") {
        auto j = record.to_json();
        auto restored = TaskRecord::from_json(j);
        CHECK(restored.id == record.id);
        CHECK(restored.name == record.name);
        CHECK(restored.status == record.status);
        CHECK(restored.origin == record.origin);
        CHECK(restored.agent_id == record.agent_id);
        CHECK(restored.session_key == record.session_key);
        CHECK(restored.flow_id == record.flow_id);
    }

    SECTION("from_json handles missing optional fields") {
        json j = {{"id", "x"}, {"name", "y"}, {"status", "pending"}};
        auto r = TaskRecord::from_json(j);
        CHECK(r.id == "x");
        CHECK(r.name == "y");
        CHECK(!r.agent_id.has_value());
        CHECK(!r.session_key.has_value());
        CHECK(!r.flow_id.has_value());
    }
}

// --- TaskFlow JSON tests ---

TEST_CASE("TaskFlow JSON serialization", "[tasks]") {
    TaskFlow flow;
    flow.id = "flow_abc";
    flow.name = "test-flow";
    flow.status = "completed";
    flow.total_tasks = 5;
    flow.completed_tasks = 4;
    flow.failed_tasks = 1;
    flow.created_at = 1700000000000;
    flow.updated_at = 1700000005000;

    SECTION("to_json preserves all fields") {
        auto j = flow.to_json();
        CHECK(j["id"] == "flow_abc");
        CHECK(j["name"] == "test-flow");
        CHECK(j["status"] == "completed");
        CHECK(j["totalTasks"] == 5);
        CHECK(j["completedTasks"] == 4);
        CHECK(j["failedTasks"] == 1);
    }

    SECTION("from_json round-trips") {
        auto j = flow.to_json();
        auto restored = TaskFlow::from_json(j);
        CHECK(restored.id == flow.id);
        CHECK(restored.name == flow.name);
        CHECK(restored.status == flow.status);
        CHECK(restored.total_tasks == flow.total_tasks);
        CHECK(restored.completed_tasks == flow.completed_tasks);
        CHECK(restored.failed_tasks == flow.failed_tasks);
    }
}

// --- TaskListParams defaults ---

TEST_CASE("TaskListParams defaults", "[tasks]") {
    TaskListParams params;
    CHECK(params.limit == 50);
    CHECK(params.offset == 0);
    CHECK(!params.status_filter.has_value());
    CHECK(!params.agent_id.has_value());
    CHECK(!params.flow_id.has_value());
    CHECK(!params.origin_filter.has_value());
    CHECK(params.sort_by == "created_at");
    CHECK(params.sort_dir == "desc");
    CHECK(params.include_completed == false);
}
