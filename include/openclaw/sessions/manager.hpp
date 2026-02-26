#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/error.hpp"
#include "openclaw/sessions/session.hpp"
#include "openclaw/sessions/store.hpp"

namespace openclaw::sessions {

using boost::asio::awaitable;

/// v2026.2.25: Configuration for session fork behavior.
struct SessionForkConfig {
    /// Maximum token count for parent fork before starting a fresh child session.
    /// When the parent session exceeds this threshold, should_skip_parent_fork()
    /// returns true, causing the orchestrator to start a fresh child session
    /// instead of forking from the oversized parent.
    int64_t parent_fork_max_tokens = 100000;
};

/// v2026.2.25: Checks if a parent fork should be skipped due to token overflow.
/// Returns true if `token_count` exceeds the configured threshold.
[[nodiscard]] inline auto should_skip_parent_fork(
    int64_t token_count, const SessionForkConfig& config = {}) -> bool {
    return token_count > config.parent_fork_max_tokens;
}

class SessionManager {
public:
    explicit SessionManager(std::unique_ptr<SessionStore> store);

    auto create_session(std::string_view user_id, std::string_view device_id)
        -> awaitable<Result<SessionData>>;

    auto create_session(std::string_view user_id, std::string_view device_id,
                        std::string_view channel)
        -> awaitable<Result<SessionData>>;

    auto get_session(std::string_view id) -> awaitable<Result<SessionData>>;

    auto touch_session(std::string_view id) -> awaitable<Result<void>>;

    auto end_session(std::string_view id) -> awaitable<Result<void>>;

    auto list_sessions(std::string_view user_id)
        -> awaitable<Result<std::vector<SessionData>>>;

    auto cleanup_expired(int ttl_seconds) -> awaitable<Result<size_t>>;

    /// Increment compaction counter after successful compaction.
    auto record_compaction(std::string_view session_id) -> awaitable<Result<void>>;

    /// v2026.2.24: Cache a bootstrap file snapshot for the given session key.
    void cache_bootstrap(std::string_view session_key, std::string snapshot);

    /// v2026.2.24: Retrieve a cached bootstrap snapshot, or empty string if not cached.
    [[nodiscard]] auto get_cached_bootstrap(std::string_view session_key) const -> std::string;

    /// v2026.2.24: Invalidate cached bootstrap for a session (called on /new, /reset).
    void invalidate_bootstrap_cache(std::string_view session_key);

private:
    std::unique_ptr<SessionStore> store_;
    std::unordered_map<std::string, std::string> bootstrap_cache_;
};

} // namespace openclaw::sessions
