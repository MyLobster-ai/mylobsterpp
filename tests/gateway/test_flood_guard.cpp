#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/flood_guard.hpp"

using namespace openclaw::gateway;

TEST_CASE("FloodGuard starts at zero rejections", "[gateway][flood_guard]") {
    UnauthorizedFloodGuard guard;
    CHECK(guard.count() == 0);
    CHECK_FALSE(guard.is_flooded());
}

TEST_CASE("FloodGuard tracks rejection count", "[gateway][flood_guard]") {
    UnauthorizedFloodGuard guard;
    guard.record_rejection();
    CHECK(guard.count() == 1);
    guard.record_rejection();
    CHECK(guard.count() == 2);
}

TEST_CASE("FloodGuard triggers at threshold", "[gateway][flood_guard]") {
    UnauthorizedFloodGuard guard(5);  // Low threshold for testing

    for (int i = 0; i < 4; ++i) {
        CHECK_FALSE(guard.record_rejection());
    }

    // 5th rejection hits threshold
    CHECK(guard.record_rejection());
    CHECK(guard.is_flooded());
}

TEST_CASE("FloodGuard default threshold is 50", "[gateway][flood_guard]") {
    UnauthorizedFloodGuard guard;

    for (int i = 0; i < 49; ++i) {
        CHECK_FALSE(guard.record_rejection());
    }

    CHECK(guard.record_rejection());  // 50th
}

TEST_CASE("FloodGuard reset clears count", "[gateway][flood_guard]") {
    UnauthorizedFloodGuard guard(5);

    for (int i = 0; i < 3; ++i) {
        guard.record_rejection();
    }

    guard.reset();
    CHECK(guard.count() == 0);
    CHECK_FALSE(guard.is_flooded());
}
