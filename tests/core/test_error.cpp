#include <catch2/catch_test_macros.hpp>

#include "openclaw/core/error.hpp"

TEST_CASE("Error creation and accessors", "[error]") {
    SECTION("basic error") {
        openclaw::Error err(openclaw::ErrorCode::NotFound, "resource not found");
        CHECK(err.code() == openclaw::ErrorCode::NotFound);
        CHECK(err.message() == "resource not found");
        CHECK(err.detail() == "");
        CHECK(err.what() == "resource not found");
    }

    SECTION("error with detail") {
        openclaw::Error err(openclaw::ErrorCode::DatabaseError,
                           "query failed", "connection refused");
        CHECK(err.code() == openclaw::ErrorCode::DatabaseError);
        CHECK(err.message() == "query failed");
        CHECK(err.detail() == "connection refused");
        CHECK(err.what() == "query failed: connection refused");
    }
}

TEST_CASE("make_error helpers", "[error]") {
    SECTION("two-argument form") {
        auto err = openclaw::make_error(openclaw::ErrorCode::Unauthorized, "not authenticated");
        CHECK(err.code() == openclaw::ErrorCode::Unauthorized);
        CHECK(err.message() == "not authenticated");
        CHECK(err.detail() == "");
    }

    SECTION("three-argument form") {
        auto err = openclaw::make_error(openclaw::ErrorCode::Timeout,
                                       "request timed out", "after 30s");
        CHECK(err.code() == openclaw::ErrorCode::Timeout);
        CHECK(err.what() == "request timed out: after 30s");
    }
}

TEST_CASE("Result type success case", "[error]") {
    openclaw::Result<int> result = 42;

    REQUIRE(result.has_value());
    CHECK(*result == 42);
}

TEST_CASE("Result type error case", "[error]") {
    openclaw::Result<int> result = std::unexpected(
        openclaw::make_error(openclaw::ErrorCode::InvalidArgument, "bad value"));

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == openclaw::ErrorCode::InvalidArgument);
    CHECK(result.error().message() == "bad value");
}

TEST_CASE("VoidResult success and error", "[error]") {
    SECTION("success") {
        openclaw::VoidResult result{};
        REQUIRE(result.has_value());
    }

    SECTION("error") {
        openclaw::VoidResult result = std::unexpected(
            openclaw::make_error(openclaw::ErrorCode::IoError, "disk full"));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == openclaw::ErrorCode::IoError);
    }
}

TEST_CASE("ErrorCode covers expected codes", "[error]") {
    // Verify that the enum values are distinct
    auto to_int = [](openclaw::ErrorCode c) { return static_cast<int>(c); };

    CHECK(to_int(openclaw::ErrorCode::Unknown) == 1);
    CHECK(to_int(openclaw::ErrorCode::InvalidConfig) == 2);
    CHECK(to_int(openclaw::ErrorCode::NotFound) == 4);
    CHECK(to_int(openclaw::ErrorCode::Unauthorized) == 6);
    CHECK(to_int(openclaw::ErrorCode::ProviderError) == 15);
    CHECK(to_int(openclaw::ErrorCode::RateLimited) == 21);
    CHECK(to_int(openclaw::ErrorCode::InternalError) == 22);
}
