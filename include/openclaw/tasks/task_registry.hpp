#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/error.hpp"

namespace openclaw::tasks {

using boost::asio::awaitable;
using json = nlohmann::json;

/// v2026.3.31: Task execution status.
enum class TaskStatus {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled,
    Blocked,
    Lost,
};

/// Convert TaskStatus to/from string.
[[nodiscard]] auto task_status_to_string(TaskStatus status) -> std::string;
[[nodiscard]] auto string_to_task_status(std::string_view s) -> TaskStatus;

/// v2026.3.31: Task origin — how the task was created.
enum class TaskOrigin {
    Cron,
    Subagent,
    Acp,
    Background,
    Manual,
};

[[nodiscard]] auto task_origin_to_string(TaskOrigin origin) -> std::string;
[[nodiscard]] auto string_to_task_origin(std::string_view s) -> TaskOrigin;

/// v2026.3.31: A single background task record.
struct TaskRecord {
    std::string id;
    std::string name;
    TaskStatus status = TaskStatus::Pending;
    TaskOrigin origin = TaskOrigin::Manual;
    std::optional<std::string> agent_id;
    std::optional<std::string> session_key;
    std::optional<std::string> flow_id;
    std::optional<std::string> parent_task_id;
    std::string error_message;
    int64_t created_at = 0;   // epoch ms
    int64_t updated_at = 0;   // epoch ms
    int64_t started_at = 0;   // epoch ms (0 = not started)
    int64_t completed_at = 0; // epoch ms (0 = not completed)

    [[nodiscard]] auto to_json() const -> json;
    static auto from_json(const json& j) -> TaskRecord;
};

/// v2026.3.31: A task flow groups related tasks.
struct TaskFlow {
    std::string id;
    std::string name;
    std::string status = "open"; // "open", "blocked", "completed", "failed"
    int total_tasks = 0;
    int completed_tasks = 0;
    int failed_tasks = 0;
    int64_t created_at = 0;
    int64_t updated_at = 0;

    [[nodiscard]] auto to_json() const -> json;
    static auto from_json(const json& j) -> TaskFlow;
};

/// v2026.3.31: Parameters for listing tasks.
struct TaskListParams {
    int limit = 50;
    int offset = 0;
    std::optional<std::string> status_filter;
    std::optional<std::string> agent_id;
    std::optional<std::string> flow_id;
    std::optional<std::string> origin_filter;
    std::string sort_by = "created_at";
    std::string sort_dir = "desc";
    bool include_completed = false;  // v2026.4.1: hide stale completed by default
};

/// v2026.3.31/v2026.4.1: SQLite-backed background task registry.
///
/// Provides unified task lifecycle management for ACP, subagent, cron,
/// and background CLI execution. Persists state in SQLite for crash recovery.
class TaskRegistry {
public:
    explicit TaskRegistry(std::string_view db_path,
                          boost::asio::io_context& ioc);
    ~TaskRegistry();

    TaskRegistry(const TaskRegistry&) = delete;
    TaskRegistry& operator=(const TaskRegistry&) = delete;

    /// Create a new task record. Returns the task ID.
    auto create_task(std::string_view name, TaskOrigin origin,
                     std::optional<std::string_view> agent_id = std::nullopt,
                     std::optional<std::string_view> session_key = std::nullopt,
                     std::optional<std::string_view> flow_id = std::nullopt)
        -> Result<std::string>;

    /// Get a task by ID.
    auto get_task(std::string_view task_id) -> Result<TaskRecord>;

    /// Update task status with optional error message.
    auto update_status(std::string_view task_id, TaskStatus status,
                       std::string_view error_message = "")
        -> Result<void>;

    /// Cancel a running or pending task.
    auto cancel_task(std::string_view task_id) -> Result<void>;

    /// List tasks with filtering and paging.
    auto list_tasks(const TaskListParams& params) -> std::vector<TaskRecord>;

    /// Get active task count for an agent.
    [[nodiscard]] auto active_count(std::string_view agent_id = "") const -> int;

    /// Get recent failure count for agent-local fallback (v2026.4.1).
    [[nodiscard]] auto recent_failure_count(
        std::string_view agent_id,
        int window_seconds = 3600) const -> int;

    // --- Task Flow management ---

    /// Create a new task flow. Returns the flow ID.
    auto create_flow(std::string_view name) -> Result<std::string>;

    /// Get a flow by ID.
    auto get_flow(std::string_view flow_id) -> Result<TaskFlow>;

    /// Link a task to a flow.
    auto link_task_to_flow(std::string_view task_id,
                           std::string_view flow_id) -> Result<void>;

    /// Update flow status based on linked task states.
    auto sync_flow_status(std::string_view flow_id) -> Result<void>;

    // --- Maintenance ---

    /// v2026.4.1: Maintenance sweep — mark stale running tasks as lost,
    /// prune old completed tasks. Designed to not stall the event loop.
    auto maintenance_sweep(int stale_threshold_seconds = 3600,
                           int prune_age_seconds = 86400) -> int;

    /// v2026.4.1: Re-check a task record before marking as lost or pruning.
    auto recheck_before_mark(std::string_view task_id) -> bool;

    /// Total task count.
    [[nodiscard]] auto total_count() const -> size_t;

private:
    void init_schema();
    auto now_ms() const -> int64_t;
    auto generate_id() const -> std::string;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    boost::asio::io_context& ioc_;
    mutable std::mutex mutex_;
};

} // namespace openclaw::tasks
