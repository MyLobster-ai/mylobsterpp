#include "openclaw/cron/scheduler.hpp"
#include "openclaw/core/logger.hpp"

#include <algorithm>
#include <chrono>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace openclaw::cron {

using boost::asio::awaitable;
using boost::asio::use_awaitable;

CronScheduler::CronScheduler(boost::asio::io_context& ioc)
    : ioc_(ioc)
{
}

CronScheduler::~CronScheduler() {
    stop();
}

auto CronScheduler::schedule(std::string_view name, std::string_view cron_expr,
                              Task task, bool delete_after_run) -> Result<void>
{
    if (name.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Task name must not be empty"));
    }

    // Sanitize job ID to prevent path traversal
    std::string sanitized_name(name);
    std::erase_if(sanitized_name, [](char c) { return c == '/' || c == '\\'; });
    // Remove ".." sequences
    while (sanitized_name.find("..") != std::string::npos) {
        auto pos = sanitized_name.find("..");
        sanitized_name.erase(pos, 2);
    }
    if (sanitized_name.empty()) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Task name contains only path traversal characters"));
    }

    if (!task) {
        return std::unexpected(make_error(
            ErrorCode::InvalidArgument,
            "Task callback must not be null",
            sanitized_name));
    }

    // Parse the cron expression to validate it.
    auto parsed = parse_cron(cron_expr);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    std::lock_guard lock(mutex_);

    auto key = sanitized_name;
    if (tasks_.contains(key)) {
        LOG_INFO("Replacing existing cron task '{}'", sanitized_name);
    }

    tasks_.insert_or_assign(key, ScheduledTask{
        .name = key,
        .expression = std::move(*parsed),
        .task = std::move(task),
        .delete_after_run = delete_after_run,
    });

    LOG_INFO("Scheduled cron task '{}' with expression '{}'", sanitized_name, cron_expr);
    return {};
}

auto CronScheduler::cancel(std::string_view name) -> Result<void> {
    std::lock_guard lock(mutex_);

    auto it = tasks_.find(std::string(name));
    if (it == tasks_.end()) {
        return std::unexpected(make_error(
            ErrorCode::NotFound,
            "No cron task with this name",
            std::string(name)));
    }

    tasks_.erase(it);
    LOG_INFO("Cancelled cron task '{}'", name);
    return {};
}

auto CronScheduler::start() -> awaitable<void> {
    running_.store(true, std::memory_order_release);
    LOG_INFO("Cron scheduler started");

    boost::asio::steady_timer timer(ioc_);

    while (running_.load(std::memory_order_acquire)) {
        abort_requested_.store(false, std::memory_order_release);

        // v2026.3.2: Timer hot-loop guard — enforce minimum re-arm delay
        auto now_steady = std::chrono::steady_clock::now();
        if (last_tick_time_ != std::chrono::steady_clock::time_point{}) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now_steady - last_tick_time_);
            if (elapsed.count() < kMinRearmDelayMs) {
                boost::asio::steady_timer guard_timer(ioc_,
                    std::chrono::milliseconds(kMinRearmDelayMs - elapsed.count()));
                auto [guard_ec] = co_await guard_timer.async_wait(
                    boost::asio::as_tuple(use_awaitable));
                if (guard_ec == boost::asio::error::operation_aborted) break;
            }
        }

        // Compute time until the next minute boundary.
        auto now = Clock::now();
        auto current_minute = std::chrono::time_point_cast<std::chrono::minutes>(now);

        // If we are past the start of the current minute, wait until the next.
        auto next_minute = current_minute + std::chrono::minutes(1);
        auto wait_duration = next_minute - now;

        // Add a small buffer (1 second) to ensure we land after the boundary.
        timer.expires_after(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(wait_duration) +
            std::chrono::seconds(1));

        auto [ec] = co_await timer.async_wait(
            boost::asio::as_tuple(use_awaitable));

        if (ec) {
            // Timer was cancelled (likely due to stop()).
            if (ec == boost::asio::error::operation_aborted) {
                break;
            }
            LOG_WARN("Cron timer error: {}", ec.message());
            continue;
        }

        if (!running_.load(std::memory_order_acquire)) {
            break;
        }

        last_tick_time_ = std::chrono::steady_clock::now();

        // Check which tasks match the current time.
        auto tick_time = Clock::now();

        std::vector<ScheduledTask> matching;
        {
            std::lock_guard lock(mutex_);
            for (const auto& [_, task] : tasks_) {
                if (matches(task.expression, tick_time)) {
                    matching.push_back(task);
                }
            }
        }

        // Spawn each matching task as an independent coroutine.
        std::vector<std::string> to_delete;
        for (auto& entry : matching) {
            LOG_DEBUG("Firing cron task '{}'", entry.name);
            auto task_copy = entry.task;
            auto task_name = entry.name;
            bool one_shot = entry.delete_after_run;
            int stagger = entry.stagger_ms;
            int max_retries = entry.max_retries;
            int retry_backoff = entry.retry_backoff_ms;

            // v2026.3.2: Lane draining — track active tasks
            active_task_count_.fetch_add(1, std::memory_order_relaxed);

            boost::asio::co_spawn(
                ioc_,
                [this, &ioc = ioc_, task_copy = std::move(task_copy),
                 task_name = std::move(task_name), stagger,
                 max_retries, retry_backoff]()
                    -> awaitable<void> {
                    // v2026.3.2: Session reaper — ensure cleanup runs in finally block
                    struct TaskGuard {
                        std::atomic<int>& counter;
                        ~TaskGuard() { counter.fetch_sub(1, std::memory_order_relaxed); }
                    } guard{active_task_count_};

                    try {
                        // Apply stagger delay before execution
                        if (stagger > 0) {
                            boost::asio::steady_timer delay_timer(ioc,
                                std::chrono::milliseconds(stagger));
                            co_await delay_timer.async_wait(use_awaitable);
                        }
                        // Check abort flag before execution
                        if (abort_requested_.load(std::memory_order_acquire)) {
                            LOG_INFO("Cron task '{}' skipped due to abort request", task_name);
                            // v2026.3.2: Record skipped delivery status
                            {
                                std::lock_guard lock(mutex_);
                                if (auto it = run_log_.find(task_name); it != run_log_.end()) {
                                    it->second.delivery_status = DeliveryStatus::Skipped;
                                }
                            }
                            co_return;
                        }

                        // v2026.3.2: One-shot retry with bounded backoff
                        // Use flag-based loop to avoid co_await in catch blocks
                        int attempt = 0;
                        int current_backoff = retry_backoff;
                        bool succeeded = false;
                        std::string last_error;

                        while (!succeeded) {
                            bool need_retry = false;
                            try {
                                co_await task_copy();
                                succeeded = true;
                            } catch (const std::exception& e) {
                                last_error = e.what();
                                attempt++;
                                if (attempt > max_retries) {
                                    // Record failure and check alerting threshold
                                    {
                                        std::lock_guard lock(mutex_);
                                        if (auto it = run_log_.find(task_name); it != run_log_.end()) {
                                            it->second.delivery_status = DeliveryStatus::Failed;
                                            it->second.error_message = last_error;
                                            it->second.retry_attempt = attempt - 1;
                                        }
                                        if (auto it = tasks_.find(task_name); it != tasks_.end()) {
                                            it->second.consecutive_failures++;
                                            if (it->second.consecutive_failures >=
                                                it->second.failure_alert.max_consecutive_failures &&
                                                it->second.failure_alert.mode != "none") {
                                                LOG_WARN("Cron task '{}' has failed {} consecutive times",
                                                         task_name, it->second.consecutive_failures);
                                            }
                                        }
                                    }
                                    throw;  // Re-throw after max retries
                                }
                                need_retry = true;
                            }

                            if (need_retry) {
                                LOG_WARN("Cron task '{}' attempt {}/{} failed: {}, retrying in {}ms",
                                         task_name, attempt, max_retries + 1, last_error, current_backoff);
                                boost::asio::steady_timer retry_timer(ioc,
                                    std::chrono::milliseconds(current_backoff));
                                co_await retry_timer.async_wait(use_awaitable);
                                // Bounded exponential backoff (cap at 60s)
                                current_backoff = std::min(current_backoff * 2, 60000);
                            }
                        }

                        if (succeeded) {
                            // v2026.3.2: Mark delivery status as Ok
                            std::lock_guard lock(mutex_);
                            if (auto it = run_log_.find(task_name); it != run_log_.end()) {
                                it->second.completed = true;
                                it->second.delivery_status = DeliveryStatus::Ok;
                                it->second.retry_attempt = attempt;
                            }
                            // Reset consecutive failures on success
                            if (auto it = tasks_.find(task_name); it != tasks_.end()) {
                                it->second.consecutive_failures = 0;
                            }
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Cron task '{}' failed: {}", task_name, e.what());
                    }
                },
                boost::asio::detached);

            if (one_shot) {
                to_delete.push_back(entry.name);
            }
        }

        // Auto-remove one-shot tasks after successful firing
        if (!to_delete.empty()) {
            std::lock_guard lock(mutex_);
            for (const auto& name : to_delete) {
                tasks_.erase(name);
                LOG_INFO("Auto-deleted one-shot cron task '{}' after execution", name);
            }
        }
    }

    // v2026.3.2: Lane draining — wait for active tasks to complete (30s hard timeout)
    if (active_task_count_.load(std::memory_order_relaxed) > 0) {
        LOG_INFO("Cron scheduler draining {} active tasks...",
                 active_task_count_.load(std::memory_order_relaxed));
        boost::asio::steady_timer drain_timer(ioc_);
        for (int i = 0; i < 60; ++i) {
            if (active_task_count_.load(std::memory_order_relaxed) == 0) break;
            drain_timer.expires_after(std::chrono::milliseconds(500));
            auto [drain_ec] = co_await drain_timer.async_wait(
                boost::asio::as_tuple(use_awaitable));
            if (drain_ec) break;
        }
        if (active_task_count_.load(std::memory_order_relaxed) > 0) {
            LOG_WARN("Cron scheduler stopped with {} tasks still running",
                     active_task_count_.load(std::memory_order_relaxed));
        }
    }

    LOG_INFO("Cron scheduler stopped");
    co_return;
}

auto CronScheduler::stop() -> void {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        LOG_INFO("Cron scheduler stopping...");
    }
}

auto CronScheduler::is_running() const noexcept -> bool {
    return running_.load(std::memory_order_acquire);
}

auto CronScheduler::task_names() const -> std::vector<std::string> {
    std::lock_guard lock(mutex_);
    std::vector<std::string> names;
    names.reserve(tasks_.size());
    for (const auto& [name, _] : tasks_) {
        names.push_back(name);
    }
    return names;
}

auto CronScheduler::size() const noexcept -> size_t {
    std::lock_guard lock(mutex_);
    return tasks_.size();
}

auto CronScheduler::manual_run(std::string_view name) -> Result<void> {
    std::lock_guard lock(mutex_);
    auto it = tasks_.find(std::string(name));
    if (it == tasks_.end()) {
        return std::unexpected(make_error(
            ErrorCode::NotFound, "No cron task with this name", std::string(name)));
    }

    // Record manual run marker before releasing mutex
    run_log_[std::string(name)] = RunEntry{
        .name = std::string(name),
        .started_at = std::chrono::steady_clock::now(),
        .completed = false,
    };

    auto task_copy = it->second.task;
    auto task_name = std::string(name);

    boost::asio::co_spawn(
        ioc_,
        [this, task_copy = std::move(task_copy), task_name]() -> awaitable<void> {
            try {
                // Check abort flag before execution
                if (abort_requested_.load(std::memory_order_acquire)) {
                    LOG_INFO("Manual run of '{}' aborted before execution", task_name);
                    // v2026.3.2: Record skipped delivery status
                    std::lock_guard lock(mutex_);
                    if (auto it = run_log_.find(task_name); it != run_log_.end()) {
                        it->second.delivery_status = DeliveryStatus::Skipped;
                    }
                    co_return;
                }
                co_await task_copy();
                // Mark as completed in run log
                std::lock_guard lock(mutex_);
                if (auto it = run_log_.find(task_name); it != run_log_.end()) {
                    it->second.completed = true;
                    it->second.delivery_status = DeliveryStatus::Ok;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Manual run of '{}' failed: {}", task_name, e.what());
                // v2026.3.2: Record failure delivery status
                std::lock_guard lock(mutex_);
                if (auto it = run_log_.find(task_name); it != run_log_.end()) {
                    it->second.delivery_status = DeliveryStatus::Failed;
                    it->second.error_message = e.what();
                }
            }
        },
        boost::asio::detached);

    LOG_INFO("Manually triggered cron task '{}'", name);
    return {};
}

void CronScheduler::abort_current() {
    abort_requested_.store(true, std::memory_order_release);
    LOG_INFO("Cron abort requested");
}

void CronScheduler::clean_run_log() {
    std::lock_guard lock(mutex_);
    std::erase_if(run_log_, [](const auto& pair) {
        return pair.second.completed;
    });
}

auto CronScheduler::list(const CronListParams& params) const -> std::vector<std::string> {
    std::lock_guard lock(mutex_);

    // Collect all task names
    std::vector<std::string> names;
    names.reserve(tasks_.size());
    for (const auto& [name, _] : tasks_) {
        // Apply query filter (substring match on name)
        if (params.query && !params.query->empty()) {
            if (name.find(*params.query) == std::string::npos) {
                continue;
            }
        }
        names.push_back(name);
    }

    // Sort
    if (params.sort_by == "name") {
        if (params.sort_dir == "desc") {
            std::sort(names.begin(), names.end(), std::greater<>());
        } else {
            std::sort(names.begin(), names.end());
        }
    }

    // Apply paging
    int offset = std::max(0, params.offset);
    int limit = std::max(0, params.limit);

    if (offset >= static_cast<int>(names.size())) {
        return {};
    }

    auto begin = names.begin() + offset;
    auto end = (offset + limit < static_cast<int>(names.size()))
        ? names.begin() + offset + limit
        : names.end();

    return std::vector<std::string>(begin, end);
}

auto CronScheduler::list_runs(const CronRunsParams& params) const -> std::vector<RunEntry> {
    std::lock_guard lock(mutex_);

    // Collect all run entries
    std::vector<RunEntry> entries;
    entries.reserve(run_log_.size());
    for (const auto& [_, entry] : run_log_) {
        // Apply query filter (substring match on name)
        if (params.query && !params.query->empty()) {
            if (entry.name.find(*params.query) == std::string::npos) {
                continue;
            }
        }
        // Apply status filter
        if (params.statuses) {
            std::string status = entry.completed ? "completed" : "running";
            bool found = false;
            for (const auto& s : *params.statuses) {
                if (s == status) { found = true; break; }
            }
            if (!found) continue;
        }
        // v2026.3.2: Apply delivery status filter
        if (params.delivery_statuses) {
            auto ds_str = [](DeliveryStatus ds) -> std::string {
                switch (ds) {
                    case DeliveryStatus::Pending: return "pending";
                    case DeliveryStatus::Ok: return "ok";
                    case DeliveryStatus::Failed: return "failed";
                    case DeliveryStatus::Skipped: return "skipped";
                }
                return "pending";
            };
            std::string ds = ds_str(entry.delivery_status);
            bool found = false;
            for (const auto& s : *params.delivery_statuses) {
                if (s == ds) { found = true; break; }
            }
            if (!found) continue;
        }
        entries.push_back(entry);
    }

    // Sort by started_at
    if (params.sort_dir == "asc") {
        std::sort(entries.begin(), entries.end(),
            [](const RunEntry& a, const RunEntry& b) {
                return a.started_at < b.started_at;
            });
    } else {
        std::sort(entries.begin(), entries.end(),
            [](const RunEntry& a, const RunEntry& b) {
                return a.started_at > b.started_at;
            });
    }

    // Apply paging
    int offset = std::max(0, params.offset);
    int limit = std::max(0, params.limit);

    if (offset >= static_cast<int>(entries.size())) {
        return {};
    }

    auto begin = entries.begin() + offset;
    auto end = (offset + limit < static_cast<int>(entries.size()))
        ? entries.begin() + offset + limit
        : entries.end();

    return std::vector<RunEntry>(begin, end);
}

} // namespace openclaw::cron
