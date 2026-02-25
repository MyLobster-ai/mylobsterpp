#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/cron/parser.hpp"

namespace openclaw::cron {

using boost::asio::awaitable;

/// Cron-based task scheduler.
///
/// Runs within a Boost.Asio io_context. Tasks are registered with a cron
/// expression and a coroutine callback. The scheduler evaluates all entries
/// once per minute, spawning any whose expression matches the current time.
class CronScheduler {
public:
    /// Coroutine task type: an async function with no arguments returning void.
    using Task = std::function<awaitable<void>()>;

    /// Construct a scheduler tied to the given io_context.
    explicit CronScheduler(boost::asio::io_context& ioc);

    ~CronScheduler();

    // Non-copyable, non-movable.
    CronScheduler(const CronScheduler&) = delete;
    CronScheduler& operator=(const CronScheduler&) = delete;

    /// Schedule a new recurring task.
    ///
    /// @param name              Unique name for the task (used for cancel/replace).
    /// @param cron_expr         Standard 5-field cron expression.
    /// @param task              Coroutine to invoke on each match.
    /// @param delete_after_run  If true, auto-cancel after first successful execution.
    /// @returns                 Error if the cron expression is invalid or the name is empty.
    auto schedule(std::string_view name, std::string_view cron_expr, Task task,
                  bool delete_after_run = false)
        -> Result<void>;

    /// Cancel a previously scheduled task by name.
    ///
    /// @param name  The task name passed to schedule().
    /// @returns     Error if no task with this name exists.
    auto cancel(std::string_view name) -> Result<void>;

    /// Start the scheduler's run loop.
    /// This coroutine ticks once per minute and fires matching tasks.
    /// It runs until stop() is called.
    auto start() -> awaitable<void>;

    /// Signal the scheduler to stop after the current tick completes.
    auto stop() -> void;

    /// Returns true if the scheduler is currently running.
    [[nodiscard]] auto is_running() const noexcept -> bool;

    /// Returns the names of all scheduled tasks.
    [[nodiscard]] auto task_names() const -> std::vector<std::string>;

    /// Returns the number of scheduled tasks.
    [[nodiscard]] auto size() const noexcept -> size_t;

    /// Manually trigger a task to run immediately.
    auto manual_run(std::string_view name) -> Result<void>;

    /// Request abort of the current running task (best-effort).
    void abort_current();

    /// Remove completed entries from the run log.
    void clean_run_log();

    // v2026.2.24: Paging and filtering for cron list/runs

    /// A run-log entry tracking task execution.
    struct RunEntry {
        std::string name;
        std::chrono::steady_clock::time_point started_at;
        bool completed = false;
    };

    /// Parameters for listing cron jobs with paging/filtering.
    struct CronListParams {
        int limit = 50;
        int offset = 0;
        std::optional<std::string> query;         // name filter
        std::optional<bool> enabled;              // enabled filter
        std::string sort_by = "name";             // "name", "created_at"
        std::string sort_dir = "asc";             // "asc", "desc"
    };

    /// Parameters for listing cron runs with paging/filtering.
    struct CronRunsParams {
        int limit = 50;
        int offset = 0;
        std::optional<std::string> query;
        std::optional<std::vector<std::string>> statuses;
        std::optional<std::vector<std::string>> delivery_statuses;
        std::optional<std::string> scope;
        std::string sort_by = "started_at";
        std::string sort_dir = "desc";
    };

    /// List scheduled tasks with paging and filtering.
    [[nodiscard]] auto list(const CronListParams& params) const -> std::vector<std::string>;

    /// List run log entries with paging and filtering.
    [[nodiscard]] auto list_runs(const CronRunsParams& params) const -> std::vector<RunEntry>;

private:
    struct ScheduledTask {
        std::string name;
        CronExpression expression;
        Task task;
        bool delete_after_run = false;  // Auto-cancel after successful execution
        int stagger_ms = 0;             // Delay before execution (jitter)
    };

    boost::asio::io_context& ioc_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ScheduledTask> tasks_;
    std::atomic<bool> running_{false};
    std::atomic<bool> abort_requested_{false};
    std::unordered_map<std::string, RunEntry> run_log_;

    int startup_timeout_ms_ = 60000;  // 60s default
};

} // namespace openclaw::cron
