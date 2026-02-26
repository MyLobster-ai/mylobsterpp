#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/auth.hpp"
#include "openclaw/gateway/hooks.hpp"

using namespace openclaw::gateway;

// v2026.2.25: Browser auth policy tests

TEST_CASE("Browser auth: empty allowed_origins allows all", "[gateway][auth]") {
    BrowserAuthPolicy policy;
    policy.allowed_origins = {};  // empty = all allowed

    CHECK(validate_browser_ws_origin("http://localhost:3000", policy));
    CHECK(validate_browser_ws_origin("https://example.com", policy));
    CHECK(validate_browser_ws_origin("", policy));  // empty is also allowed
}

TEST_CASE("Browser auth: specific origins restrict access", "[gateway][auth]") {
    BrowserAuthPolicy policy;
    policy.allowed_origins = {"http://localhost:3000", "https://app.example.com"};

    CHECK(validate_browser_ws_origin("http://localhost:3000", policy));
    CHECK(validate_browser_ws_origin("https://app.example.com", policy));
    CHECK_FALSE(validate_browser_ws_origin("https://evil.com", policy));
    CHECK_FALSE(validate_browser_ws_origin("", policy));
}

TEST_CASE("Browser auth: loopback throttle", "[gateway][auth]") {
    BrowserAuthPolicy policy;
    policy.allow_loopback = true;
    policy.max_loopback_connections = 5;

    CHECK(check_loopback_browser_throttle(0, policy));
    CHECK(check_loopback_browser_throttle(4, policy));
    CHECK_FALSE(check_loopback_browser_throttle(5, policy));
    CHECK_FALSE(check_loopback_browser_throttle(10, policy));
}

TEST_CASE("Browser auth: loopback disabled", "[gateway][auth]") {
    BrowserAuthPolicy policy;
    policy.allow_loopback = false;

    CHECK_FALSE(check_loopback_browser_throttle(0, policy));
}

// v2026.2.25: Trusted proxy + operator role tests

TEST_CASE("Trusted proxy: operator role required for control-ui", "[gateway][auth]") {
    AuthInfo auth;
    auth.trusted_proxy_auth_ok = true;

    CHECK(should_skip_control_ui_pairing(auth, true, "operator"));
    CHECK_FALSE(should_skip_control_ui_pairing(auth, true, "viewer"));
    CHECK_FALSE(should_skip_control_ui_pairing(auth, true, ""));
}

TEST_CASE("Trusted proxy: non-control-ui always false", "[gateway][auth]") {
    AuthInfo auth;
    auth.trusted_proxy_auth_ok = true;

    CHECK_FALSE(should_skip_control_ui_pairing(auth, false, "operator"));
}

// v2026.2.25: Webhook URL validation tests

TEST_CASE("Webhook URL: valid URLs pass", "[gateway][hooks]") {
    CHECK(HookRegistry::validate_webhook_url("https://example.com/webhook"));
    CHECK(HookRegistry::validate_webhook_url("http://localhost:8080/hook"));
    CHECK(HookRegistry::validate_webhook_url("https://api.example.com:443/v1/notify"));
}

TEST_CASE("Webhook URL: empty URL rejected", "[gateway][hooks]") {
    CHECK_FALSE(HookRegistry::validate_webhook_url(""));
}

TEST_CASE("Webhook URL: missing scheme rejected", "[gateway][hooks]") {
    CHECK_FALSE(HookRegistry::validate_webhook_url("example.com/webhook"));
}

TEST_CASE("Webhook URL: userinfo rejected", "[gateway][hooks]") {
    CHECK_FALSE(HookRegistry::validate_webhook_url("https://user:pass@example.com/webhook"));
}

TEST_CASE("Webhook URL: empty host rejected", "[gateway][hooks]") {
    CHECK_FALSE(HookRegistry::validate_webhook_url("https:///webhook"));
}

TEST_CASE("Webhook URL: encoded traversal rejected", "[gateway][hooks]") {
    CHECK_FALSE(HookRegistry::validate_webhook_url("https://example.com/%2e%2e/etc/passwd"));
    CHECK_FALSE(HookRegistry::validate_webhook_url("https://example.com/path%2ftraversal"));
    CHECK_FALSE(HookRegistry::validate_webhook_url("https://example.com/%5c..%5c"));
}
