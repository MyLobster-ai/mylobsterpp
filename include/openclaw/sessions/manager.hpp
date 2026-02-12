#pragma once

#include <memory>
#include <string_view>

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

private:
    std::unique_ptr<SessionStore> store_;
};

} // namespace openclaw::sessions
