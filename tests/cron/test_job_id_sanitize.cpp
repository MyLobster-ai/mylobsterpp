#include <catch2/catch_test_macros.hpp>
#include "openclaw/cron/scheduler.hpp"

TEST_CASE("CronScheduler job ID sanitization", "[cron][security]") {
    boost::asio::io_context ioc;
    openclaw::cron::CronScheduler scheduler(ioc);

    SECTION("Path traversal in job name is stripped") {
        auto result = scheduler.schedule("../../etc/passwd", "* * * * *",
            []() -> boost::asio::awaitable<void> { co_return; });
        // Should succeed with sanitized name
        CHECK(result.has_value());
        // The sanitized name should not contain path components
        auto names = scheduler.task_names();
        REQUIRE(names.size() == 1);
        CHECK(names[0].find("..") == std::string::npos);
        CHECK(names[0].find("/") == std::string::npos);
    }

    SECTION("Slashes in job name are stripped") {
        auto result = scheduler.schedule("path/to/job", "* * * * *",
            []() -> boost::asio::awaitable<void> { co_return; });
        CHECK(result.has_value());
        auto names = scheduler.task_names();
        REQUIRE(names.size() == 1);
        CHECK(names[0].find("/") == std::string::npos);
    }

    SECTION("Backslashes in job name are stripped") {
        auto result = scheduler.schedule("path\\to\\job", "* * * * *",
            []() -> boost::asio::awaitable<void> { co_return; });
        CHECK(result.has_value());
        auto names = scheduler.task_names();
        REQUIRE(names.size() == 1);
        CHECK(names[0].find("\\") == std::string::npos);
    }

    SECTION("Name with only traversal chars fails") {
        auto result = scheduler.schedule("../../", "* * * * *",
            []() -> boost::asio::awaitable<void> { co_return; });
        CHECK_FALSE(result.has_value());
    }
}
