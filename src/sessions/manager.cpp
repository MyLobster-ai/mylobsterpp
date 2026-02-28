#include "openclaw/sessions/manager.hpp"

#include <chrono>
#include <future>

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::sessions {

// v2026.2.26: Run a cleanup operation with a timeout.
// Returns "ok" on success, "timeout" if the timeout was hit,
// or "error" with the error message.
enum class CleanupResult { Ok, Timeout, Error };

static auto run_cleanup_with_timeout(
    std::function<void()> op,
    std::chrono::seconds duration) -> CleanupResult
{
    auto future = std::async(std::launch::async, [op = std::move(op)]() {
        try {
            op();
        } catch (const std::exception& e) {
            LOG_ERROR("Session cleanup error: {}", e.what());
            throw;
        }
    });

    auto status = future.wait_for(duration);
    if (status == std::future_status::timeout) {
        LOG_WARN("Session cleanup timed out after {}s", duration.count());
        return CleanupResult::Timeout;
    }

    try {
        future.get();  // propagate exception if any
        return CleanupResult::Ok;
    } catch (...) {
        return CleanupResult::Error;
    }
}

SessionManager::SessionManager(std::unique_ptr<SessionStore> store)
    : store_(std::move(store)) {
    LOG_INFO("Session manager initialized");
}

auto SessionManager::create_session(std::string_view user_id,
                                    std::string_view device_id)
    -> awaitable<Result<SessionData>> {
    return create_session(user_id, device_id, "");
}

auto SessionManager::create_session(std::string_view user_id,
                                    std::string_view device_id,
                                    std::string_view channel)
    -> awaitable<Result<SessionData>> {
    auto now = Clock::now();

    SessionData data;
    data.session.id = utils::generate_uuid();
    data.session.user_id = std::string(user_id);
    data.session.device_id = std::string(device_id);
    data.session.created_at = now;
    data.session.last_active = now;
    data.state = SessionState::Active;
    data.metadata = json::object();

    if (!channel.empty()) {
        data.session.channel = std::string(channel);
    }

    auto result = co_await store_->create(data);
    if (!result) {
        LOG_ERROR("Failed to create session for user {}: {}",
                  user_id, result.error().what());
        co_return make_fail(result.error());
    }

    LOG_INFO("Created session {} for user {} on device {}",
             data.session.id, user_id, device_id);
    co_return data;
}

auto SessionManager::get_session(std::string_view id)
    -> awaitable<Result<SessionData>> {
    auto result = co_await store_->get(id);
    if (!result) {
        LOG_DEBUG("Session {} not found", id);
        co_return make_fail(result.error());
    }
    co_return *result;
}

auto SessionManager::touch_session(std::string_view id)
    -> awaitable<Result<void>> {
    auto result = co_await store_->get(id);
    if (!result) {
        co_return make_fail(result.error());
    }

    auto data = std::move(*result);

    if (data.state == SessionState::Closed) {
        co_return make_fail(
            make_error(ErrorCode::SessionError, "Cannot touch a closed session",
                       std::string(id)));
    }

    data.session.last_active = Clock::now();
    data.state = SessionState::Active;

    auto update_result = co_await store_->update(data);
    if (!update_result) {
        co_return make_fail(update_result.error());
    }

    LOG_DEBUG("Touched session {}", id);
    co_return ok_result();
}

auto SessionManager::end_session(std::string_view id)
    -> awaitable<Result<void>> {
    auto result = co_await store_->get(id);
    if (!result) {
        co_return make_fail(result.error());
    }

    auto data = std::move(*result);
    data.state = SessionState::Closed;
    data.session.last_active = Clock::now();

    auto update_result = co_await store_->update(data);
    if (!update_result) {
        co_return make_fail(update_result.error());
    }

    // v2026.2.26: Run ACP cleanup with 15s timeout.
    // Errors are logged but not propagated (graceful degradation).
    auto session_id_str = std::string(id);
    auto cleanup_result = run_cleanup_with_timeout(
        [sid = session_id_str]() {
            // Cancel any active agent runs tied to this session.
            // This is a placeholder — actual ACP cancellation depends on
            // the agent runtime integration.
            LOG_DEBUG("Running ACP cleanup for session {}", sid);
        },
        std::chrono::seconds(15));

    if (cleanup_result == CleanupResult::Timeout) {
        LOG_WARN("ACP cleanup for session {} timed out", id);
    } else if (cleanup_result == CleanupResult::Error) {
        LOG_WARN("ACP cleanup for session {} failed", id);
    }

    LOG_INFO("Ended session {}", id);
    co_return ok_result();
}

auto SessionManager::list_sessions(std::string_view user_id)
    -> awaitable<Result<std::vector<SessionData>>> {
    co_return co_await store_->list(user_id);
}

auto SessionManager::cleanup_expired(int ttl_seconds)
    -> awaitable<Result<size_t>> {
    LOG_DEBUG("Running session cleanup with TTL={}s", ttl_seconds);
    co_return co_await store_->remove_expired(ttl_seconds);
}

auto SessionManager::record_compaction(std::string_view session_id)
    -> awaitable<Result<void>> {
    auto result = co_await store_->get(session_id);
    if (!result) {
        co_return make_fail(result.error());
    }

    auto data = std::move(*result);

    if (data.state == SessionState::Closed) {
        co_return make_fail(
            make_error(ErrorCode::SessionError,
                       "Cannot record compaction on a closed session",
                       std::string(session_id)));
    }

    // Only increment on successful compaction completion
    data.auto_compaction_count += 1;
    data.session.last_active = Clock::now();

    auto update_result = co_await store_->update(data);
    if (!update_result) {
        co_return make_fail(update_result.error());
    }

    LOG_DEBUG("Recorded compaction #{} for session {}",
              data.auto_compaction_count, session_id);
    co_return ok_result();
}

void SessionManager::cache_bootstrap(std::string_view session_key, std::string snapshot) {
    bootstrap_cache_[std::string(session_key)] = std::move(snapshot);
}

auto SessionManager::get_cached_bootstrap(std::string_view session_key) const -> std::string {
    auto it = bootstrap_cache_.find(std::string(session_key));
    if (it != bootstrap_cache_.end()) {
        return it->second;
    }
    return {};
}

void SessionManager::invalidate_bootstrap_cache(std::string_view session_key) {
    bootstrap_cache_.erase(std::string(session_key));
}

} // namespace openclaw::sessions
