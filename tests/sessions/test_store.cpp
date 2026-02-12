#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include <boost/asio.hpp>

#include "openclaw/sessions/store.hpp"

using namespace openclaw::sessions;
using json = nlohmann::json;

// Helper to run a coroutine synchronously in tests.
template <typename T>
T run_sync(boost::asio::awaitable<T> coro) {
    boost::asio::io_context ioc;
    T result;
    boost::asio::co_spawn(ioc,
        [&]() -> boost::asio::awaitable<void> {
            result = co_await std::move(coro);
        },
        boost::asio::detached);
    ioc.run();
    return result;
}

// Specialization for void results.
template <>
void run_sync(boost::asio::awaitable<void> coro) {
    boost::asio::io_context ioc;
    boost::asio::co_spawn(ioc, std::move(coro), boost::asio::detached);
    ioc.run();
}

static auto make_session_data(const std::string& id,
                               const std::string& user_id,
                               SessionState state = SessionState::Active) -> SessionData {
    SessionData data;
    data.session.id = id;
    data.session.user_id = user_id;
    data.session.device_id = "test-device";
    data.session.created_at = openclaw::Clock::now();
    data.session.last_active = openclaw::Clock::now();
    data.state = state;
    data.metadata = json::object();
    return data;
}

TEST_CASE("SqliteSessionStore create and get", "[sessions][store]") {
    auto tmp = std::filesystem::temp_directory_path() / "test_sessions_create.db";
    std::filesystem::remove(tmp);

    SqliteSessionStore store(tmp.string());
    auto data = make_session_data("s1", "user1");

    auto create_result = run_sync(store.create(data));
    REQUIRE(create_result.has_value());

    auto get_result = run_sync(store.get("s1"));
    REQUIRE(get_result.has_value());
    CHECK(get_result->session.id == "s1");
    CHECK(get_result->session.user_id == "user1");
    CHECK(get_result->session.device_id == "test-device");
    CHECK(get_result->state == SessionState::Active);

    std::filesystem::remove(tmp);
}

TEST_CASE("SqliteSessionStore get nonexistent returns NotFound", "[sessions][store]") {
    auto tmp = std::filesystem::temp_directory_path() / "test_sessions_notfound.db";
    std::filesystem::remove(tmp);

    SqliteSessionStore store(tmp.string());

    auto result = run_sync(store.get("nonexistent"));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == openclaw::ErrorCode::NotFound);

    std::filesystem::remove(tmp);
}

TEST_CASE("SqliteSessionStore update", "[sessions][store]") {
    auto tmp = std::filesystem::temp_directory_path() / "test_sessions_update.db";
    std::filesystem::remove(tmp);

    SqliteSessionStore store(tmp.string());
    auto data = make_session_data("s2", "user2");

    run_sync(store.create(data));

    data.state = SessionState::Idle;
    data.metadata = json{{"updated", true}};
    data.session.last_active = openclaw::Clock::now();

    auto update_result = run_sync(store.update(data));
    REQUIRE(update_result.has_value());

    auto get_result = run_sync(store.get("s2"));
    REQUIRE(get_result.has_value());
    CHECK(get_result->state == SessionState::Idle);
    CHECK(get_result->metadata["updated"] == true);

    std::filesystem::remove(tmp);
}

TEST_CASE("SqliteSessionStore remove", "[sessions][store]") {
    auto tmp = std::filesystem::temp_directory_path() / "test_sessions_remove.db";
    std::filesystem::remove(tmp);

    SqliteSessionStore store(tmp.string());
    auto data = make_session_data("s3", "user3");

    run_sync(store.create(data));

    SECTION("remove existing succeeds") {
        auto result = run_sync(store.remove("s3"));
        REQUIRE(result.has_value());

        auto get_result = run_sync(store.get("s3"));
        CHECK_FALSE(get_result.has_value());
    }

    SECTION("remove nonexistent returns NotFound") {
        auto result = run_sync(store.remove("nonexistent"));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == openclaw::ErrorCode::NotFound);
    }

    std::filesystem::remove(tmp);
}

TEST_CASE("SqliteSessionStore list by user", "[sessions][store]") {
    auto tmp = std::filesystem::temp_directory_path() / "test_sessions_list.db";
    std::filesystem::remove(tmp);

    SqliteSessionStore store(tmp.string());

    run_sync(store.create(make_session_data("s10", "alice")));
    run_sync(store.create(make_session_data("s11", "alice")));
    run_sync(store.create(make_session_data("s12", "bob")));

    auto alice_sessions = run_sync(store.list("alice"));
    REQUIRE(alice_sessions.has_value());
    CHECK(alice_sessions->size() == 2);

    auto bob_sessions = run_sync(store.list("bob"));
    REQUIRE(bob_sessions.has_value());
    CHECK(bob_sessions->size() == 1);

    auto empty_sessions = run_sync(store.list("nobody"));
    REQUIRE(empty_sessions.has_value());
    CHECK(empty_sessions->empty());

    std::filesystem::remove(tmp);
}
