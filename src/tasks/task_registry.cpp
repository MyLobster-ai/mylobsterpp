#include "openclaw/tasks/task_registry.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#include <SQLiteCpp/SQLiteCpp.h>
#include <spdlog/spdlog.h>

#include "openclaw/core/logger.hpp"

// Generate UUIDs
#include <random>
#include <sstream>
#include <iomanip>

namespace openclaw::tasks {

// --- Enum conversions ---

auto task_status_to_string(TaskStatus status) -> std::string {
    switch (status) {
        case TaskStatus::Pending:   return "pending";
        case TaskStatus::Running:   return "running";
        case TaskStatus::Completed: return "completed";
        case TaskStatus::Failed:    return "failed";
        case TaskStatus::Cancelled: return "cancelled";
        case TaskStatus::Blocked:   return "blocked";
        case TaskStatus::Lost:      return "lost";
    }
    return "unknown";
}

auto string_to_task_status(std::string_view s) -> TaskStatus {
    if (s == "running")   return TaskStatus::Running;
    if (s == "completed") return TaskStatus::Completed;
    if (s == "failed")    return TaskStatus::Failed;
    if (s == "cancelled") return TaskStatus::Cancelled;
    if (s == "blocked")   return TaskStatus::Blocked;
    if (s == "lost")      return TaskStatus::Lost;
    return TaskStatus::Pending;
}

auto task_origin_to_string(TaskOrigin origin) -> std::string {
    switch (origin) {
        case TaskOrigin::Cron:       return "cron";
        case TaskOrigin::Subagent:   return "subagent";
        case TaskOrigin::Acp:        return "acp";
        case TaskOrigin::Background: return "background";
        case TaskOrigin::Manual:     return "manual";
    }
    return "manual";
}

auto string_to_task_origin(std::string_view s) -> TaskOrigin {
    if (s == "cron")       return TaskOrigin::Cron;
    if (s == "subagent")   return TaskOrigin::Subagent;
    if (s == "acp")        return TaskOrigin::Acp;
    if (s == "background") return TaskOrigin::Background;
    return TaskOrigin::Manual;
}

// --- TaskRecord JSON ---

auto TaskRecord::to_json() const -> json {
    return json{
        {"id", id},
        {"name", name},
        {"status", task_status_to_string(status)},
        {"origin", task_origin_to_string(origin)},
        {"agentId", agent_id.value_or("")},
        {"sessionKey", session_key.value_or("")},
        {"flowId", flow_id.value_or("")},
        {"parentTaskId", parent_task_id.value_or("")},
        {"errorMessage", error_message},
        {"createdAt", created_at},
        {"updatedAt", updated_at},
        {"startedAt", started_at},
        {"completedAt", completed_at},
    };
}

auto TaskRecord::from_json(const json& j) -> TaskRecord {
    TaskRecord r;
    r.id = j.value("id", "");
    r.name = j.value("name", "");
    r.status = string_to_task_status(j.value("status", "pending"));
    r.origin = string_to_task_origin(j.value("origin", "manual"));
    auto aid = j.value("agentId", "");
    if (!aid.empty()) r.agent_id = aid;
    auto sk = j.value("sessionKey", "");
    if (!sk.empty()) r.session_key = sk;
    auto fid = j.value("flowId", "");
    if (!fid.empty()) r.flow_id = fid;
    auto pid = j.value("parentTaskId", "");
    if (!pid.empty()) r.parent_task_id = pid;
    r.error_message = j.value("errorMessage", "");
    r.created_at = j.value("createdAt", int64_t{0});
    r.updated_at = j.value("updatedAt", int64_t{0});
    r.started_at = j.value("startedAt", int64_t{0});
    r.completed_at = j.value("completedAt", int64_t{0});
    return r;
}

// --- TaskFlow JSON ---

auto TaskFlow::to_json() const -> json {
    return json{
        {"id", id},
        {"name", name},
        {"status", status},
        {"totalTasks", total_tasks},
        {"completedTasks", completed_tasks},
        {"failedTasks", failed_tasks},
        {"createdAt", created_at},
        {"updatedAt", updated_at},
    };
}

auto TaskFlow::from_json(const json& j) -> TaskFlow {
    TaskFlow f;
    f.id = j.value("id", "");
    f.name = j.value("name", "");
    f.status = j.value("status", "open");
    f.total_tasks = j.value("totalTasks", 0);
    f.completed_tasks = j.value("completedTasks", 0);
    f.failed_tasks = j.value("failedTasks", 0);
    f.created_at = j.value("createdAt", int64_t{0});
    f.updated_at = j.value("updatedAt", int64_t{0});
    return f;
}

// --- TaskRegistry implementation ---

/// SQLite busy-retry helper (matches session store pattern).
template<typename F>
static auto with_busy_retry(F&& fn, int max_retries = 5) -> decltype(fn()) {
    int delay_ms = 50;
    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        try {
            return fn();
        } catch (const SQLite::Exception& e) {
            if (e.getErrorCode() == 5 && attempt < max_retries) {  // SQLITE_BUSY
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                delay_ms = std::min(delay_ms * 2, 5000);
                continue;
            }
            throw;
        }
    }
    return fn();  // unreachable, but satisfies compiler
}

struct TaskRegistry::Impl {
    std::unique_ptr<SQLite::Database> db;
};

TaskRegistry::TaskRegistry(std::string_view db_path,
                           boost::asio::io_context& ioc)
    : impl_(std::make_unique<Impl>()), ioc_(ioc)
{
    impl_->db = std::make_unique<SQLite::Database>(
        std::string(db_path),
        SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE
    );
    impl_->db->setBusyTimeout(5000);
    init_schema();
    LOG_INFO("Task registry initialized at {}", db_path);
}

TaskRegistry::~TaskRegistry() = default;

void TaskRegistry::init_schema() {
    impl_->db->exec(R"SQL(
        CREATE TABLE IF NOT EXISTS tasks (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'pending',
            origin TEXT NOT NULL DEFAULT 'manual',
            agent_id TEXT,
            session_key TEXT,
            flow_id TEXT,
            parent_task_id TEXT,
            error_message TEXT DEFAULT '',
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            started_at INTEGER DEFAULT 0,
            completed_at INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_tasks_status ON tasks(status);
        CREATE INDEX IF NOT EXISTS idx_tasks_agent ON tasks(agent_id);
        CREATE INDEX IF NOT EXISTS idx_tasks_flow ON tasks(flow_id);
        CREATE INDEX IF NOT EXISTS idx_tasks_created ON tasks(created_at);

        CREATE TABLE IF NOT EXISTS task_flows (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'open',
            total_tasks INTEGER DEFAULT 0,
            completed_tasks INTEGER DEFAULT 0,
            failed_tasks INTEGER DEFAULT 0,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_flows_status ON task_flows(status);
    )SQL");
}

auto TaskRegistry::now_ms() const -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

auto TaskRegistry::generate_id() const -> std::string {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << "task_" << std::hex << std::setfill('0')
        << std::setw(16) << dist(rng);
    return oss.str();
}

auto TaskRegistry::create_task(std::string_view name, TaskOrigin origin,
                                std::optional<std::string_view> agent_id,
                                std::optional<std::string_view> session_key,
                                std::optional<std::string_view> flow_id)
    -> Result<std::string>
{
    std::lock_guard lock(mutex_);
    auto id = generate_id();
    auto ts = now_ms();

    return with_busy_retry([&]() -> Result<std::string> {
        SQLite::Statement stmt(*impl_->db,
            "INSERT INTO tasks (id, name, status, origin, agent_id, session_key, "
            "flow_id, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, id);
        stmt.bind(2, std::string(name));
        stmt.bind(3, task_status_to_string(TaskStatus::Pending));
        stmt.bind(4, task_origin_to_string(origin));
        if (agent_id) stmt.bind(5, std::string(*agent_id));
        else stmt.bind(5);
        if (session_key) stmt.bind(6, std::string(*session_key));
        else stmt.bind(6);
        if (flow_id) stmt.bind(7, std::string(*flow_id));
        else stmt.bind(7);
        stmt.bind(8, ts);
        stmt.bind(9, ts);
        stmt.exec();

        // If linked to a flow, increment its total_tasks
        if (flow_id) {
            SQLite::Statement flow_stmt(*impl_->db,
                "UPDATE task_flows SET total_tasks = total_tasks + 1, "
                "updated_at = ? WHERE id = ?");
            flow_stmt.bind(1, ts);
            flow_stmt.bind(2, std::string(*flow_id));
            flow_stmt.exec();
        }

        return id;
    });
}

auto TaskRegistry::get_task(std::string_view task_id) -> Result<TaskRecord> {
    std::lock_guard lock(mutex_);

    return with_busy_retry([&]() -> Result<TaskRecord> {
        SQLite::Statement stmt(*impl_->db,
            "SELECT id, name, status, origin, agent_id, session_key, flow_id, "
            "parent_task_id, error_message, created_at, updated_at, started_at, "
            "completed_at FROM tasks WHERE id = ?");
        stmt.bind(1, std::string(task_id));

        if (!stmt.executeStep()) {
            return std::unexpected(make_error(ErrorCode::NotFound, "task not found: " + std::string(task_id)));
        }

        TaskRecord r;
        r.id = stmt.getColumn(0).getString();
        r.name = stmt.getColumn(1).getString();
        r.status = string_to_task_status(stmt.getColumn(2).getString());
        r.origin = string_to_task_origin(stmt.getColumn(3).getString());
        if (!stmt.getColumn(4).isNull()) r.agent_id = stmt.getColumn(4).getString();
        if (!stmt.getColumn(5).isNull()) r.session_key = stmt.getColumn(5).getString();
        if (!stmt.getColumn(6).isNull()) r.flow_id = stmt.getColumn(6).getString();
        if (!stmt.getColumn(7).isNull()) r.parent_task_id = stmt.getColumn(7).getString();
        r.error_message = stmt.getColumn(8).getString();
        r.created_at = stmt.getColumn(9).getInt64();
        r.updated_at = stmt.getColumn(10).getInt64();
        r.started_at = stmt.getColumn(11).getInt64();
        r.completed_at = stmt.getColumn(12).getInt64();
        return r;
    });
}

auto TaskRegistry::update_status(std::string_view task_id, TaskStatus status,
                                  std::string_view error_message) -> Result<void> {
    std::lock_guard lock(mutex_);
    auto ts = now_ms();

    return with_busy_retry([&]() -> Result<void> {
        std::string sql = "UPDATE tasks SET status = ?, updated_at = ?, error_message = ?";
        if (status == TaskStatus::Running) {
            sql += ", started_at = ?";
        } else if (status == TaskStatus::Completed || status == TaskStatus::Failed ||
                   status == TaskStatus::Cancelled) {
            sql += ", completed_at = ?";
        }
        sql += " WHERE id = ?";

        SQLite::Statement stmt(*impl_->db, sql);
        stmt.bind(1, task_status_to_string(status));
        stmt.bind(2, ts);
        stmt.bind(3, std::string(error_message));

        int next_idx = 4;
        if (status == TaskStatus::Running ||
            status == TaskStatus::Completed ||
            status == TaskStatus::Failed ||
            status == TaskStatus::Cancelled) {
            stmt.bind(next_idx++, ts);
        }
        stmt.bind(next_idx, std::string(task_id));
        auto changed = stmt.exec();

        if (changed == 0) {
            return std::unexpected(make_error(ErrorCode::NotFound, "task not found: " + std::string(task_id)));
        }

        // Sync parent flow if linked
        SQLite::Statement flow_check(*impl_->db,
            "SELECT flow_id FROM tasks WHERE id = ?");
        flow_check.bind(1, std::string(task_id));
        if (flow_check.executeStep() && !flow_check.getColumn(0).isNull()) {
            auto flow_id = flow_check.getColumn(0).getString();
            // Update flow counters
            if (status == TaskStatus::Completed) {
                SQLite::Statement fs(*impl_->db,
                    "UPDATE task_flows SET completed_tasks = completed_tasks + 1, "
                    "updated_at = ? WHERE id = ?");
                fs.bind(1, ts);
                fs.bind(2, flow_id);
                fs.exec();
            } else if (status == TaskStatus::Failed) {
                SQLite::Statement fs(*impl_->db,
                    "UPDATE task_flows SET failed_tasks = failed_tasks + 1, "
                    "updated_at = ? WHERE id = ?");
                fs.bind(1, ts);
                fs.bind(2, flow_id);
                fs.exec();
            }
        }

        return ok_result();
    });
}

auto TaskRegistry::cancel_task(std::string_view task_id) -> Result<void> {
    auto result = get_task(task_id);
    if (!result) return std::unexpected(make_error(ErrorCode::InternalError, result.error().what()));

    auto& task = *result;
    if (task.status == TaskStatus::Completed ||
        task.status == TaskStatus::Cancelled) {
        return std::unexpected(make_error(ErrorCode::InvalidArgument, "task already finished: " + std::string(task_id)));
    }

    return update_status(task_id, TaskStatus::Cancelled, "cancelled by user");
}

auto TaskRegistry::list_tasks(const TaskListParams& params) -> std::vector<TaskRecord> {
    std::lock_guard lock(mutex_);

    return with_busy_retry([&]() -> std::vector<TaskRecord> {
        std::string sql = "SELECT id, name, status, origin, agent_id, session_key, "
            "flow_id, parent_task_id, error_message, created_at, updated_at, "
            "started_at, completed_at FROM tasks WHERE 1=1";

        std::vector<std::string> binds;

        if (params.status_filter) {
            sql += " AND status = ?";
            binds.push_back(*params.status_filter);
        }
        if (params.agent_id) {
            sql += " AND agent_id = ?";
            binds.push_back(*params.agent_id);
        }
        if (params.flow_id) {
            sql += " AND flow_id = ?";
            binds.push_back(*params.flow_id);
        }
        if (params.origin_filter) {
            sql += " AND origin = ?";
            binds.push_back(*params.origin_filter);
        }
        // v2026.4.1: Hide stale completed tasks by default
        if (!params.include_completed) {
            sql += " AND (status NOT IN ('completed', 'cancelled', 'lost') "
                   "OR updated_at > ?)";
            auto cutoff = now_ms() - 3600000;  // 1 hour
            binds.push_back(std::to_string(cutoff));
        }

        sql += " ORDER BY " + params.sort_by + " " + params.sort_dir;
        sql += " LIMIT ? OFFSET ?";

        SQLite::Statement stmt(*impl_->db, sql);
        int idx = 1;
        for (const auto& b : binds) {
            // Numeric bind for the cutoff timestamp
            if (idx == static_cast<int>(binds.size()) && !params.include_completed
                && !params.status_filter && !params.origin_filter) {
                stmt.bind(idx++, static_cast<int64_t>(std::stoll(b)));
            } else {
                stmt.bind(idx++, b);
            }
        }
        stmt.bind(idx++, params.limit);
        stmt.bind(idx, params.offset);

        std::vector<TaskRecord> results;
        while (stmt.executeStep()) {
            TaskRecord r;
            r.id = stmt.getColumn(0).getString();
            r.name = stmt.getColumn(1).getString();
            r.status = string_to_task_status(stmt.getColumn(2).getString());
            r.origin = string_to_task_origin(stmt.getColumn(3).getString());
            if (!stmt.getColumn(4).isNull()) r.agent_id = stmt.getColumn(4).getString();
            if (!stmt.getColumn(5).isNull()) r.session_key = stmt.getColumn(5).getString();
            if (!stmt.getColumn(6).isNull()) r.flow_id = stmt.getColumn(6).getString();
            if (!stmt.getColumn(7).isNull()) r.parent_task_id = stmt.getColumn(7).getString();
            r.error_message = stmt.getColumn(8).getString();
            r.created_at = stmt.getColumn(9).getInt64();
            r.updated_at = stmt.getColumn(10).getInt64();
            r.started_at = stmt.getColumn(11).getInt64();
            r.completed_at = stmt.getColumn(12).getInt64();
            results.push_back(std::move(r));
        }
        return results;
    });
}

auto TaskRegistry::active_count(std::string_view agent_id) const -> int {
    std::lock_guard lock(mutex_);

    return with_busy_retry([&]() -> int {
        std::string sql = "SELECT COUNT(*) FROM tasks WHERE status IN ('pending', 'running')";
        if (!agent_id.empty()) {
            sql += " AND agent_id = ?";
        }
        SQLite::Statement stmt(*impl_->db, sql);
        if (!agent_id.empty()) {
            stmt.bind(1, std::string(agent_id));
        }
        if (stmt.executeStep()) {
            return stmt.getColumn(0).getInt();
        }
        return 0;
    });
}

auto TaskRegistry::recent_failure_count(std::string_view agent_id,
                                         int window_seconds) const -> int {
    std::lock_guard lock(mutex_);
    auto cutoff = now_ms() - (static_cast<int64_t>(window_seconds) * 1000);

    return with_busy_retry([&]() -> int {
        std::string sql = "SELECT COUNT(*) FROM tasks WHERE status = 'failed' "
            "AND completed_at > ?";
        if (!agent_id.empty()) {
            sql += " AND agent_id = ?";
        }
        SQLite::Statement stmt(*impl_->db, sql);
        stmt.bind(1, cutoff);
        if (!agent_id.empty()) {
            stmt.bind(2, std::string(agent_id));
        }
        if (stmt.executeStep()) {
            return stmt.getColumn(0).getInt();
        }
        return 0;
    });
}

auto TaskRegistry::create_flow(std::string_view name) -> Result<std::string> {
    std::lock_guard lock(mutex_);
    auto id = "flow_" + generate_id().substr(5);
    auto ts = now_ms();

    return with_busy_retry([&]() -> Result<std::string> {
        SQLite::Statement stmt(*impl_->db,
            "INSERT INTO task_flows (id, name, status, created_at, updated_at) "
            "VALUES (?, ?, 'open', ?, ?)");
        stmt.bind(1, id);
        stmt.bind(2, std::string(name));
        stmt.bind(3, ts);
        stmt.bind(4, ts);
        stmt.exec();
        return id;
    });
}

auto TaskRegistry::get_flow(std::string_view flow_id) -> Result<TaskFlow> {
    std::lock_guard lock(mutex_);

    return with_busy_retry([&]() -> Result<TaskFlow> {
        SQLite::Statement stmt(*impl_->db,
            "SELECT id, name, status, total_tasks, completed_tasks, failed_tasks, "
            "created_at, updated_at FROM task_flows WHERE id = ?");
        stmt.bind(1, std::string(flow_id));

        if (!stmt.executeStep()) {
            return std::unexpected(make_error(ErrorCode::NotFound, "flow not found: " + std::string(flow_id)));
        }

        TaskFlow f;
        f.id = stmt.getColumn(0).getString();
        f.name = stmt.getColumn(1).getString();
        f.status = stmt.getColumn(2).getString();
        f.total_tasks = stmt.getColumn(3).getInt();
        f.completed_tasks = stmt.getColumn(4).getInt();
        f.failed_tasks = stmt.getColumn(5).getInt();
        f.created_at = stmt.getColumn(6).getInt64();
        f.updated_at = stmt.getColumn(7).getInt64();
        return f;
    });
}

auto TaskRegistry::link_task_to_flow(std::string_view task_id,
                                      std::string_view flow_id) -> Result<void> {
    std::lock_guard lock(mutex_);
    auto ts = now_ms();

    return with_busy_retry([&]() -> Result<void> {
        SQLite::Statement stmt(*impl_->db,
            "UPDATE tasks SET flow_id = ?, updated_at = ? WHERE id = ?");
        stmt.bind(1, std::string(flow_id));
        stmt.bind(2, ts);
        stmt.bind(3, std::string(task_id));
        auto changed = stmt.exec();
        if (changed == 0) {
            return std::unexpected(make_error(ErrorCode::NotFound, "task not found"));
        }

        SQLite::Statement fs(*impl_->db,
            "UPDATE task_flows SET total_tasks = total_tasks + 1, updated_at = ? "
            "WHERE id = ?");
        fs.bind(1, ts);
        fs.bind(2, std::string(flow_id));
        fs.exec();

        return ok_result();
    });
}

auto TaskRegistry::sync_flow_status(std::string_view flow_id) -> Result<void> {
    std::lock_guard lock(mutex_);
    auto ts = now_ms();

    return with_busy_retry([&]() -> Result<void> {
        // Count task states in this flow
        SQLite::Statement count_stmt(*impl_->db,
            "SELECT status, COUNT(*) FROM tasks WHERE flow_id = ? GROUP BY status");
        count_stmt.bind(1, std::string(flow_id));

        int total = 0, completed = 0, failed = 0, running = 0, blocked = 0;
        while (count_stmt.executeStep()) {
            auto s = string_to_task_status(count_stmt.getColumn(0).getString());
            auto c = count_stmt.getColumn(1).getInt();
            total += c;
            switch (s) {
                case TaskStatus::Completed: completed += c; break;
                case TaskStatus::Failed:    failed += c; break;
                case TaskStatus::Running:   running += c; break;
                case TaskStatus::Blocked:   blocked += c; break;
                default: break;
            }
        }

        std::string new_status = "open";
        if (total > 0 && completed + failed == total) {
            new_status = failed > 0 ? "failed" : "completed";
        } else if (blocked > 0 && running == 0) {
            new_status = "blocked";
        }

        SQLite::Statement up(*impl_->db,
            "UPDATE task_flows SET status = ?, total_tasks = ?, "
            "completed_tasks = ?, failed_tasks = ?, updated_at = ? WHERE id = ?");
        up.bind(1, new_status);
        up.bind(2, total);
        up.bind(3, completed);
        up.bind(4, failed);
        up.bind(5, ts);
        up.bind(6, std::string(flow_id));
        up.exec();

        return ok_result();
    });
}

auto TaskRegistry::maintenance_sweep(int stale_threshold_seconds,
                                      int prune_age_seconds) -> int {
    std::lock_guard lock(mutex_);
    auto ts = now_ms();
    auto stale_cutoff = ts - (static_cast<int64_t>(stale_threshold_seconds) * 1000);
    auto prune_cutoff = ts - (static_cast<int64_t>(prune_age_seconds) * 1000);
    int affected = 0;

    try {
        // v2026.4.1: Re-check before marking as lost
        {
            SQLite::Statement stmt(*impl_->db,
                "SELECT id FROM tasks WHERE status = 'running' AND updated_at < ?");
            stmt.bind(1, stale_cutoff);
            std::vector<std::string> stale_ids;
            while (stmt.executeStep()) {
                stale_ids.push_back(stmt.getColumn(0).getString());
            }
            for (const auto& id : stale_ids) {
                // Re-check current state before marking
                SQLite::Statement check(*impl_->db,
                    "SELECT status, updated_at FROM tasks WHERE id = ?");
                check.bind(1, id);
                if (check.executeStep()) {
                    auto cur_status = check.getColumn(0).getString();
                    auto cur_updated = check.getColumn(1).getInt64();
                    if (cur_status == "running" && cur_updated < stale_cutoff) {
                        SQLite::Statement mark(*impl_->db,
                            "UPDATE tasks SET status = 'lost', updated_at = ?, "
                            "completed_at = ? WHERE id = ?");
                        mark.bind(1, ts);
                        mark.bind(2, ts);
                        mark.bind(3, id);
                        affected += mark.exec();
                    }
                }
            }
        }

        // Prune old completed/cancelled/lost tasks
        {
            SQLite::Statement stmt(*impl_->db,
                "DELETE FROM tasks WHERE status IN ('completed', 'cancelled', 'lost') "
                "AND completed_at > 0 AND completed_at < ?");
            stmt.bind(1, prune_cutoff);
            affected += stmt.exec();
        }

        // Prune empty completed flows
        {
            SQLite::Statement stmt(*impl_->db,
                "DELETE FROM task_flows WHERE status IN ('completed', 'failed') "
                "AND updated_at < ?");
            stmt.bind(1, prune_cutoff);
            affected += stmt.exec();
        }

    } catch (const SQLite::Exception& e) {
        LOG_WARN("Task registry maintenance error: {}", e.what());
    }

    if (affected > 0) {
        LOG_DEBUG("Task maintenance sweep: {} records affected", affected);
    }
    return affected;
}

auto TaskRegistry::recheck_before_mark(std::string_view task_id) -> bool {
    std::lock_guard lock(mutex_);

    return with_busy_retry([&]() -> bool {
        SQLite::Statement stmt(*impl_->db,
            "SELECT status FROM tasks WHERE id = ?");
        stmt.bind(1, std::string(task_id));
        if (stmt.executeStep()) {
            auto s = stmt.getColumn(0).getString();
            return s == "running";  // only mark if still running
        }
        return false;
    });
}

auto TaskRegistry::total_count() const -> size_t {
    std::lock_guard lock(mutex_);

    return with_busy_retry([&]() -> size_t {
        SQLite::Statement stmt(*impl_->db, "SELECT COUNT(*) FROM tasks");
        if (stmt.executeStep()) {
            return static_cast<size_t>(stmt.getColumn(0).getInt64());
        }
        return 0;
    });
}

} // namespace openclaw::tasks
