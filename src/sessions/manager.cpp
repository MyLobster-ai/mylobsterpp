#include "openclaw/sessions/manager.hpp"

#include <chrono>

#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw::sessions {

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
        co_return std::unexpected(result.error());
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
        co_return std::unexpected(result.error());
    }
    co_return *result;
}

auto SessionManager::touch_session(std::string_view id)
    -> awaitable<Result<void>> {
    auto result = co_await store_->get(id);
    if (!result) {
        co_return std::unexpected(result.error());
    }

    auto data = std::move(*result);

    if (data.state == SessionState::Closed) {
        co_return std::unexpected(
            make_error(ErrorCode::SessionError, "Cannot touch a closed session",
                       std::string(id)));
    }

    data.session.last_active = Clock::now();
    data.state = SessionState::Active;

    auto update_result = co_await store_->update(data);
    if (!update_result) {
        co_return std::unexpected(update_result.error());
    }

    LOG_DEBUG("Touched session {}", id);
    co_return Result<void>{};
}

auto SessionManager::end_session(std::string_view id)
    -> awaitable<Result<void>> {
    auto result = co_await store_->get(id);
    if (!result) {
        co_return std::unexpected(result.error());
    }

    auto data = std::move(*result);
    data.state = SessionState::Closed;
    data.session.last_active = Clock::now();

    auto update_result = co_await store_->update(data);
    if (!update_result) {
        co_return std::unexpected(update_result.error());
    }

    LOG_INFO("Ended session {}", id);
    co_return Result<void>{};
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
        co_return std::unexpected(result.error());
    }

    auto data = std::move(*result);

    if (data.state == SessionState::Closed) {
        co_return std::unexpected(
            make_error(ErrorCode::SessionError,
                       "Cannot record compaction on a closed session",
                       std::string(session_id)));
    }

    // Only increment on successful compaction completion
    data.auto_compaction_count += 1;
    data.session.last_active = Clock::now();

    auto update_result = co_await store_->update(data);
    if (!update_result) {
        co_return std::unexpected(update_result.error());
    }

    LOG_DEBUG("Recorded compaction #{} for session {}",
              data.auto_compaction_count, session_id);
    co_return Result<void>{};
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
