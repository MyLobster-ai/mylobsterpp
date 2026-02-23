#include <catch2/catch_test_macros.hpp>
#include "openclaw/cron/scheduler.hpp"

TEST_CASE("CronScheduler manual run", "[cron][manual_run]") {
    boost::asio::io_context ioc;
    openclaw::cron::CronScheduler scheduler(ioc);

    SECTION("Manual run of non-existent task fails") {
        auto result = scheduler.manual_run("nonexistent");
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == openclaw::ErrorCode::NotFound);
    }

    SECTION("Manual run of existing task succeeds") {
        bool ran = false;
        auto result = scheduler.schedule("test-task", "* * * * *",
            [&ran]() -> boost::asio::awaitable<void> {
                ran = true;
                co_return;
            });
        REQUIRE(result.has_value());

        auto run_result = scheduler.manual_run("test-task");
        CHECK(run_result.has_value());
        // Run io_context briefly to execute the task
        ioc.run_for(std::chrono::milliseconds(100));
        CHECK(ran);
    }
}
