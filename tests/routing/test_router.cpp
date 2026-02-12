#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include <boost/asio.hpp>

#include "openclaw/routing/router.hpp"
#include "openclaw/routing/rules.hpp"

using namespace openclaw::routing;

// Helper to run a coroutine synchronously.
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

TEST_CASE("Router starts empty", "[routing][router]") {
    Router router;
    CHECK(router.route_count() == 0);
}

TEST_CASE("Router add_route increases count", "[routing][router]") {
    Router router;

    router.add_route(
        std::make_unique<PrefixRule>("/help"),
        [](const IncomingMessage&) -> boost::asio::awaitable<void> { co_return; });

    router.add_route(
        std::make_unique<ChannelRule>("telegram"),
        [](const IncomingMessage&) -> boost::asio::awaitable<void> { co_return; });

    CHECK(router.route_count() == 2);
}

TEST_CASE("Router routes to matching prefix rule", "[routing][router]") {
    Router router;
    bool handler_called = false;

    router.add_route(
        std::make_unique<PrefixRule>("/echo"),
        [&handler_called](const IncomingMessage& msg) -> boost::asio::awaitable<void> {
            handler_called = true;
            co_return;
        });

    IncomingMessage msg{
        .channel = "test",
        .sender_id = "user1",
        .text = "/echo hello world",
        .metadata = nlohmann::json::object(),
    };

    auto result = run_sync(router.route(msg));
    CHECK(result.has_value());
    CHECK(handler_called);
}

TEST_CASE("Router returns NotFound when no route matches", "[routing][router]") {
    Router router;

    router.add_route(
        std::make_unique<PrefixRule>("/help"),
        [](const IncomingMessage&) -> boost::asio::awaitable<void> { co_return; });

    IncomingMessage msg{
        .channel = "test",
        .sender_id = "user1",
        .text = "random message",
        .metadata = nlohmann::json::object(),
    };

    auto result = run_sync(router.route(msg));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == openclaw::ErrorCode::NotFound);
}

TEST_CASE("Router clear removes all routes", "[routing][router]") {
    Router router;

    router.add_route(
        std::make_unique<PrefixRule>("/a"),
        [](const IncomingMessage&) -> boost::asio::awaitable<void> { co_return; });

    router.add_route(
        std::make_unique<PrefixRule>("/b"),
        [](const IncomingMessage&) -> boost::asio::awaitable<void> { co_return; });

    REQUIRE(router.route_count() == 2);
    router.clear();
    CHECK(router.route_count() == 0);
}

TEST_CASE("Router respects priority ordering", "[routing][router]") {
    Router router;
    std::string matched_rule;

    // Lower priority added first
    router.add_route(
        std::make_unique<ChannelRule>("telegram", 0),
        [&matched_rule](const IncomingMessage&) -> boost::asio::awaitable<void> {
            matched_rule = "channel";
            co_return;
        });

    // Higher priority added second - should still match first
    router.add_route(
        std::make_unique<PrefixRule>("/cmd", 10),
        [&matched_rule](const IncomingMessage&) -> boost::asio::awaitable<void> {
            matched_rule = "prefix";
            co_return;
        });

    IncomingMessage msg{
        .channel = "telegram",
        .sender_id = "user1",
        .text = "/cmd do something",
        .metadata = nlohmann::json::object(),
    };

    auto result = run_sync(router.route(msg));
    CHECK(result.has_value());
    // The higher priority prefix rule (priority=10) should match first
    CHECK(matched_rule == "prefix");
}
