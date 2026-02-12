#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <SQLiteCpp/SQLiteCpp.h>

#include "openclaw/core/error.hpp"
#include "openclaw/sessions/session.hpp"

namespace openclaw::sessions {

using boost::asio::awaitable;

class SessionStore {
public:
    virtual ~SessionStore() = default;

    virtual auto create(const SessionData& data) -> awaitable<Result<void>> = 0;
    virtual auto get(std::string_view id) -> awaitable<Result<SessionData>> = 0;
    virtual auto update(const SessionData& data) -> awaitable<Result<void>> = 0;
    virtual auto remove(std::string_view id) -> awaitable<Result<void>> = 0;
    virtual auto list(std::string_view user_id) -> awaitable<Result<std::vector<SessionData>>> = 0;
    virtual auto remove_expired(int ttl_seconds) -> awaitable<Result<size_t>> = 0;
};

class SqliteSessionStore : public SessionStore {
public:
    explicit SqliteSessionStore(const std::string& db_path);

    auto create(const SessionData& data) -> awaitable<Result<void>> override;
    auto get(std::string_view id) -> awaitable<Result<SessionData>> override;
    auto update(const SessionData& data) -> awaitable<Result<void>> override;
    auto remove(std::string_view id) -> awaitable<Result<void>> override;
    auto list(std::string_view user_id) -> awaitable<Result<std::vector<SessionData>>> override;
    auto remove_expired(int ttl_seconds) -> awaitable<Result<size_t>> override;

private:
    void init_schema();
    auto state_to_string(SessionState state) const -> std::string;
    auto string_to_state(std::string_view s) const -> SessionState;
    auto timestamp_to_ms(Timestamp ts) const -> int64_t;
    auto ms_to_timestamp(int64_t ms) const -> Timestamp;

    std::unique_ptr<SQLite::Database> db_;
};

} // namespace openclaw::sessions
