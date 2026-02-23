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

        // Compute time until the next minute boundary.
        auto now = Clock::now();
        auto now_seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
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

            boost::asio::co_spawn(
                ioc_,
                [this, &ioc = ioc_, task_copy = std::move(task_copy),
                 task_name = std::move(task_name), stagger]()
                    -> awaitable<void> {
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
                            co_return;
                        }
                        co_await task_copy();
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
                    co_return;
                }
                co_await task_copy();
                // Mark as completed in run log
                std::lock_guard lock(mutex_);
                if (auto it = run_log_.find(task_name); it != run_log_.end()) {
                    it->second.completed = true;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Manual run of '{}' failed: {}", task_name, e.what());
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

} // namespace openclaw::cron
