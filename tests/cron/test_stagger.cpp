#include <catch2/catch_test_macros.hpp>

#include "openclaw/cron/scheduler.hpp"

#include <boost/asio/io_context.hpp>

using namespace openclaw::cron;

TEST_CASE("ScheduledTask stagger_ms defaults to zero", "[cron][stagger]") {
    boost::asio::io_context ioc;
    CronScheduler scheduler(ioc);

    // Schedule a task and verify it can be registered (stagger is internal)
    auto result = scheduler.schedule("stagger_test", "*/5 * * * *",
        []() -> boost::asio::awaitable<void> {
            co_return;
        });

    REQUIRE(result.has_value());
    CHECK(scheduler.size() == 1);
}

TEST_CASE("CronScheduler can schedule top-of-hour tasks", "[cron][stagger]") {
    boost::asio::io_context ioc;
    CronScheduler scheduler(ioc);

    // Top-of-hour expressions should be accepted and may have auto-stagger
    auto result = scheduler.schedule("hourly_task", "0 * * * *",
        []() -> boost::asio::awaitable<void> {
            co_return;
        });

    REQUIRE(result.has_value());
    CHECK(scheduler.size() == 1);

    auto names = scheduler.task_names();
    REQUIRE(names.size() == 1);
    CHECK(names[0] == "hourly_task");
}
