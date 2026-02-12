#include "openclaw/sessions/store.hpp"

#include <chrono>

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::sessions {

SqliteSessionStore::SqliteSessionStore(const std::string& db_path)
    : db_(std::make_unique<SQLite::Database>(
          db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)) {
    init_schema();
    LOG_INFO("SQLite session store opened at {}", db_path);
}

void SqliteSessionStore::init_schema() {
    db_->exec(R"SQL(
        CREATE TABLE IF NOT EXISTS sessions (
            id TEXT PRIMARY KEY,
            user_id TEXT NOT NULL,
            device_id TEXT NOT NULL,
            channel TEXT,
            state TEXT NOT NULL DEFAULT 'active',
            metadata TEXT NOT NULL DEFAULT '{}',
            created_at INTEGER NOT NULL,
            last_active INTEGER NOT NULL
        )
    )SQL");

    db_->exec(R"SQL(
        CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON sessions(user_id)
    )SQL");

    db_->exec(R"SQL(
        CREATE INDEX IF NOT EXISTS idx_sessions_last_active ON sessions(last_active)
    )SQL");
}

auto SqliteSessionStore::state_to_string(SessionState state) const -> std::string {
    switch (state) {
        case SessionState::Active: return "active";
        case SessionState::Idle:   return "idle";
        case SessionState::Closed: return "closed";
    }
    return "active";
}

auto SqliteSessionStore::string_to_state(std::string_view s) const -> SessionState {
    if (s == "idle")   return SessionState::Idle;
    if (s == "closed") return SessionState::Closed;
    return SessionState::Active;
}

auto SqliteSessionStore::timestamp_to_ms(Timestamp ts) const -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        ts.time_since_epoch()
    ).count();
}

auto SqliteSessionStore::ms_to_timestamp(int64_t ms) const -> Timestamp {
    return Timestamp{std::chrono::milliseconds{ms}};
}

auto SqliteSessionStore::create(const SessionData& data) -> awaitable<Result<void>> {
    try {
        SQLite::Statement stmt(*db_,
            "INSERT INTO sessions (id, user_id, device_id, channel, state, metadata, "
            "created_at, last_active) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");

        stmt.bind(1, data.session.id);
        stmt.bind(2, data.session.user_id);
        stmt.bind(3, data.session.device_id);

        if (data.session.channel) {
            stmt.bind(4, *data.session.channel);
        } else {
            stmt.bind(4);  // bind NULL
        }

        stmt.bind(5, state_to_string(data.state));
        stmt.bind(6, data.metadata.dump());
        stmt.bind(7, timestamp_to_ms(data.session.created_at));
        stmt.bind(8, timestamp_to_ms(data.session.last_active));

        stmt.exec();

        LOG_DEBUG("Created session {}", data.session.id);
        co_return Result<void>{};
    } catch (const SQLite::Exception& e) {
        LOG_ERROR("Failed to create session {}: {}", data.session.id, e.what());
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError, "Failed to create session", e.what()));
    }
}

auto SqliteSessionStore::get(std::string_view id) -> awaitable<Result<SessionData>> {
    try {
        SQLite::Statement stmt(*db_,
            "SELECT id, user_id, device_id, channel, state, metadata, "
            "created_at, last_active FROM sessions WHERE id = ?");

        stmt.bind(1, std::string(id));

        if (!stmt.executeStep()) {
            co_return std::unexpected(
                make_error(ErrorCode::NotFound, "Session not found",
                           std::string(id)));
        }

        SessionData data;
        data.session.id = stmt.getColumn(0).getString();
        data.session.user_id = stmt.getColumn(1).getString();
        data.session.device_id = stmt.getColumn(2).getString();

        if (!stmt.getColumn(3).isNull()) {
            data.session.channel = stmt.getColumn(3).getString();
        }

        data.state = string_to_state(stmt.getColumn(4).getString());
        data.metadata = json::parse(stmt.getColumn(5).getString());
        data.session.created_at = ms_to_timestamp(stmt.getColumn(6).getInt64());
        data.session.last_active = ms_to_timestamp(stmt.getColumn(7).getInt64());

        co_return data;
    } catch (const SQLite::Exception& e) {
        LOG_ERROR("Failed to get session {}: {}", id, e.what());
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError, "Failed to get session", e.what()));
    }
}

auto SqliteSessionStore::update(const SessionData& data) -> awaitable<Result<void>> {
    try {
        SQLite::Statement stmt(*db_,
            "UPDATE sessions SET state = ?, metadata = ?, last_active = ?, "
            "channel = ? WHERE id = ?");

        stmt.bind(1, state_to_string(data.state));
        stmt.bind(2, data.metadata.dump());
        stmt.bind(3, timestamp_to_ms(data.session.last_active));

        if (data.session.channel) {
            stmt.bind(4, *data.session.channel);
        } else {
            stmt.bind(4);  // bind NULL
        }

        stmt.bind(5, data.session.id);

        auto rows = stmt.exec();
        if (rows == 0) {
            co_return std::unexpected(
                make_error(ErrorCode::NotFound, "Session not found",
                           data.session.id));
        }

        LOG_DEBUG("Updated session {}", data.session.id);
        co_return Result<void>{};
    } catch (const SQLite::Exception& e) {
        LOG_ERROR("Failed to update session {}: {}", data.session.id, e.what());
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError, "Failed to update session", e.what()));
    }
}

auto SqliteSessionStore::remove(std::string_view id) -> awaitable<Result<void>> {
    try {
        SQLite::Statement stmt(*db_, "DELETE FROM sessions WHERE id = ?");
        stmt.bind(1, std::string(id));

        auto rows = stmt.exec();
        if (rows == 0) {
            co_return std::unexpected(
                make_error(ErrorCode::NotFound, "Session not found",
                           std::string(id)));
        }

        LOG_DEBUG("Removed session {}", id);
        co_return Result<void>{};
    } catch (const SQLite::Exception& e) {
        LOG_ERROR("Failed to remove session {}: {}", id, e.what());
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError, "Failed to remove session", e.what()));
    }
}

auto SqliteSessionStore::list(std::string_view user_id)
    -> awaitable<Result<std::vector<SessionData>>> {
    try {
        SQLite::Statement stmt(*db_,
            "SELECT id, user_id, device_id, channel, state, metadata, "
            "created_at, last_active FROM sessions WHERE user_id = ? "
            "ORDER BY last_active DESC");

        stmt.bind(1, std::string(user_id));

        std::vector<SessionData> results;
        while (stmt.executeStep()) {
            SessionData data;
            data.session.id = stmt.getColumn(0).getString();
            data.session.user_id = stmt.getColumn(1).getString();
            data.session.device_id = stmt.getColumn(2).getString();

            if (!stmt.getColumn(3).isNull()) {
                data.session.channel = stmt.getColumn(3).getString();
            }

            data.state = string_to_state(stmt.getColumn(4).getString());
            data.metadata = json::parse(stmt.getColumn(5).getString());
            data.session.created_at = ms_to_timestamp(stmt.getColumn(6).getInt64());
            data.session.last_active = ms_to_timestamp(stmt.getColumn(7).getInt64());

            results.push_back(std::move(data));
        }

        co_return results;
    } catch (const SQLite::Exception& e) {
        LOG_ERROR("Failed to list sessions for user {}: {}", user_id, e.what());
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError, "Failed to list sessions", e.what()));
    }
}

auto SqliteSessionStore::remove_expired(int ttl_seconds)
    -> awaitable<Result<size_t>> {
    try {
        auto cutoff_ms = utils::timestamp_ms() -
            static_cast<int64_t>(ttl_seconds) * 1000;

        SQLite::Statement stmt(*db_,
            "DELETE FROM sessions WHERE last_active < ?");

        stmt.bind(1, cutoff_ms);

        auto rows = static_cast<size_t>(stmt.exec());

        if (rows > 0) {
            LOG_INFO("Cleaned up {} expired sessions (ttl={}s)", rows, ttl_seconds);
        }

        co_return rows;
    } catch (const SQLite::Exception& e) {
        LOG_ERROR("Failed to remove expired sessions: {}", e.what());
        co_return std::unexpected(
            make_error(ErrorCode::DatabaseError, "Failed to remove expired sessions",
                       e.what()));
    }
}

} // namespace openclaw::sessions
