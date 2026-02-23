#include <catch2/catch_test_macros.hpp>
#include "openclaw/cron/scheduler.hpp"

TEST_CASE("CronScheduler abort", "[cron][abort]") {
    boost::asio::io_context ioc;
    openclaw::cron::CronScheduler scheduler(ioc);

    SECTION("Abort flag is initially false") {
        // The abort flag should not prevent scheduling
        auto result = scheduler.schedule("test", "* * * * *",
            []() -> boost::asio::awaitable<void> { co_return; });
        CHECK(result.has_value());
    }

    SECTION("Abort can be called without running tasks") {
        scheduler.abort_current();  // Should not crash
    }
}
