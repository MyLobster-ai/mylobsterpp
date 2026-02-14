#include <catch2/catch_test_macros.hpp>

#include "openclaw/cron/scheduler.hpp"

#include <boost/asio/io_context.hpp>

using namespace openclaw::cron;

TEST_CASE("CronScheduler deleteAfterRun", "[cron]") {
    boost::asio::io_context ioc;
    CronScheduler scheduler(ioc);

    SECTION("Schedule with delete_after_run=true") {
        bool executed = false;
        auto result = scheduler.schedule("one_shot", "* * * * *",
            [&executed]() -> boost::asio::awaitable<void> {
                executed = true;
                co_return;
            },
            true);  // delete_after_run = true

        REQUIRE(result.has_value());
        CHECK(scheduler.size() == 1);

        // Verify the task is registered
        auto names = scheduler.task_names();
        REQUIRE(names.size() == 1);
        CHECK(names[0] == "one_shot");
    }

    SECTION("Schedule with delete_after_run=false (default)") {
        auto result = scheduler.schedule("recurring", "* * * * *",
            []() -> boost::asio::awaitable<void> {
                co_return;
            });

        REQUIRE(result.has_value());
        CHECK(scheduler.size() == 1);
    }

    SECTION("Cancel removes task") {
        scheduler.schedule("task1", "* * * * *",
            []() -> boost::asio::awaitable<void> {
                co_return;
            },
            true);

        CHECK(scheduler.size() == 1);

        auto cancel_result = scheduler.cancel("task1");
        CHECK(cancel_result.has_value());
        CHECK(scheduler.size() == 0);
    }

    SECTION("Cancel non-existent returns error") {
        auto result = scheduler.cancel("nonexistent");
        REQUIRE(!result.has_value());
        CHECK(result.error().code() == openclaw::ErrorCode::NotFound);
    }
}
