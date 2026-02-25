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
