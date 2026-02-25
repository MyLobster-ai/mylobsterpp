#include <catch2/catch_test_macros.hpp>

#include "openclaw/cron/scheduler.hpp"

#include <boost/asio/io_context.hpp>

using namespace openclaw::cron;

TEST_CASE("CronListParams defaults", "[cron][list]") {
    CronScheduler::CronListParams params;
    CHECK(params.limit == 50);
    CHECK(params.offset == 0);
    CHECK_FALSE(params.query.has_value());
    CHECK_FALSE(params.enabled.has_value());
    CHECK(params.sort_by == "name");
    CHECK(params.sort_dir == "asc");
}

TEST_CASE("CronRunsParams defaults", "[cron][list]") {
    CronScheduler::CronRunsParams params;
    CHECK(params.limit == 50);
    CHECK(params.offset == 0);
    CHECK_FALSE(params.query.has_value());
    CHECK_FALSE(params.statuses.has_value());
    CHECK_FALSE(params.delivery_statuses.has_value());
    CHECK_FALSE(params.scope.has_value());
    CHECK(params.sort_by == "started_at");
    CHECK(params.sort_dir == "desc");
}

TEST_CASE("list() returns empty for empty scheduler", "[cron][list]") {
    boost::asio::io_context ioc;
    CronScheduler sched(ioc);

    CronScheduler::CronListParams params;
    auto result = sched.list(params);
    CHECK(result.empty());
}

TEST_CASE("list() returns scheduled task names", "[cron][list]") {
    boost::asio::io_context ioc;
    CronScheduler sched(ioc);

    // Schedule some tasks
    sched.schedule("alpha", "* * * * *", []() -> boost::asio::awaitable<void> { co_return; });
    sched.schedule("beta", "* * * * *", []() -> boost::asio::awaitable<void> { co_return; });
    sched.schedule("gamma", "* * * * *", []() -> boost::asio::awaitable<void> { co_return; });

    CronScheduler::CronListParams params;
    auto result = sched.list(params);
    CHECK(result.size() == 3);
}

TEST_CASE("list() applies query filter", "[cron][list]") {
    boost::asio::io_context ioc;
    CronScheduler sched(ioc);

    sched.schedule("heartbeat-check", "* * * * *", []() -> boost::asio::awaitable<void> { co_return; });
    sched.schedule("heartbeat-send", "* * * * *", []() -> boost::asio::awaitable<void> { co_return; });
    sched.schedule("cleanup-logs", "* * * * *", []() -> boost::asio::awaitable<void> { co_return; });

    CronScheduler::CronListParams params;
    params.query = "heartbeat";
    auto result = sched.list(params);
    CHECK(result.size() == 2);
}

TEST_CASE("list() applies paging", "[cron][list]") {
    boost::asio::io_context ioc;
    CronScheduler sched(ioc);

    for (int i = 0; i < 10; ++i) {
        sched.schedule("task-" + std::to_string(i), "* * * * *",
            []() -> boost::asio::awaitable<void> { co_return; });
    }

    SECTION("limit") {
        CronScheduler::CronListParams params;
        params.limit = 3;
        auto result = sched.list(params);
        CHECK(result.size() == 3);
    }

    SECTION("offset") {
        CronScheduler::CronListParams params;
        params.offset = 8;
        auto result = sched.list(params);
        CHECK(result.size() == 2);
    }

    SECTION("offset beyond end") {
        CronScheduler::CronListParams params;
        params.offset = 100;
        auto result = sched.list(params);
        CHECK(result.empty());
    }
}

TEST_CASE("list() sorts by name", "[cron][list]") {
    boost::asio::io_context ioc;
    CronScheduler sched(ioc);

    sched.schedule("charlie", "* * * * *", []() -> boost::asio::awaitable<void> { co_return; });
    sched.schedule("alpha", "* * * * *", []() -> boost::asio::awaitable<void> { co_return; });
    sched.schedule("bravo", "* * * * *", []() -> boost::asio::awaitable<void> { co_return; });

    SECTION("ascending") {
        CronScheduler::CronListParams params;
        params.sort_dir = "asc";
        auto result = sched.list(params);
        REQUIRE(result.size() == 3);
        CHECK(result[0] == "alpha");
        CHECK(result[1] == "bravo");
        CHECK(result[2] == "charlie");
    }

    SECTION("descending") {
        CronScheduler::CronListParams params;
        params.sort_dir = "desc";
        auto result = sched.list(params);
        REQUIRE(result.size() == 3);
        CHECK(result[0] == "charlie");
        CHECK(result[1] == "bravo");
        CHECK(result[2] == "alpha");
    }
}

TEST_CASE("list_runs() returns empty for no runs", "[cron][list]") {
    boost::asio::io_context ioc;
    CronScheduler sched(ioc);

    CronScheduler::CronRunsParams params;
    auto result = sched.list_runs(params);
    CHECK(result.empty());
}
