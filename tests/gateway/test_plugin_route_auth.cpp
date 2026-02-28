#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/server.hpp"

using namespace openclaw::gateway;

TEST_CASE("AuthRateLimiter: under limit allows through", "[gateway][security]") {
    AuthRateLimiter limiter;
    CHECK_FALSE(limiter.check("192.168.1.1"));
    limiter.record_failure("192.168.1.1");
    CHECK_FALSE(limiter.check("192.168.1.1"));
}

TEST_CASE("AuthRateLimiter: at limit blocks", "[gateway][security]") {
    AuthRateLimiter limiter;
    for (int i = 0; i < AuthRateLimiter::kMaxFailures; ++i) {
        CHECK_FALSE(limiter.check("10.0.0.1"));
        limiter.record_failure("10.0.0.1");
    }
    CHECK(limiter.check("10.0.0.1"));
}

TEST_CASE("AuthRateLimiter: different IPs tracked independently", "[gateway][security]") {
    AuthRateLimiter limiter;
    for (int i = 0; i < AuthRateLimiter::kMaxFailures; ++i) {
        limiter.record_failure("10.0.0.1");
    }
    CHECK(limiter.check("10.0.0.1"));
    CHECK_FALSE(limiter.check("10.0.0.2"));
}

TEST_CASE("Protected route prefixes contain /api/channels", "[gateway][security]") {
    bool found = false;
    for (auto prefix : PROTECTED_ROUTE_PREFIXES) {
        if (prefix == "/api/channels") {
            found = true;
            break;
        }
    }
    CHECK(found);
}
