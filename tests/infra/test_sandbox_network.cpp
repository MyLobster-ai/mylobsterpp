#include <catch2/catch_test_macros.hpp>

#include "openclaw/infra/sandbox_network.hpp"

using namespace openclaw::infra;

// ---------------------------------------------------------------------------
// normalize_network_mode
// ---------------------------------------------------------------------------

TEST_CASE("normalize_network_mode lowercases input", "[sandbox_network]") {
    CHECK(normalize_network_mode("HOST") == "host");
    CHECK(normalize_network_mode("Bridge") == "bridge");
    CHECK(normalize_network_mode("Container:abc123") == "container:abc123");
}

TEST_CASE("normalize_network_mode trims whitespace", "[sandbox_network]") {
    CHECK(normalize_network_mode("  host  ") == "host");
    CHECK(normalize_network_mode("\tbridge\n") == "bridge");
}

TEST_CASE("normalize_network_mode handles empty string", "[sandbox_network]") {
    CHECK(normalize_network_mode("") == "");
    CHECK(normalize_network_mode("   ") == "");
}

// ---------------------------------------------------------------------------
// get_blocked_network_mode_reason
// ---------------------------------------------------------------------------

TEST_CASE("host mode is always blocked", "[sandbox_network]") {
    auto reason = get_blocked_network_mode_reason("host");
    REQUIRE(reason.has_value());
    CHECK(*reason == NetworkModeBlockReason::Host);
}

TEST_CASE("HOST (case-insensitive) is blocked", "[sandbox_network]") {
    auto reason = get_blocked_network_mode_reason("HOST");
    REQUIRE(reason.has_value());
    CHECK(*reason == NetworkModeBlockReason::Host);
}

TEST_CASE("container: prefix is blocked", "[sandbox_network]") {
    auto reason = get_blocked_network_mode_reason("container:abc123");
    REQUIRE(reason.has_value());
    CHECK(*reason == NetworkModeBlockReason::ContainerNamespaceJoin);
}

TEST_CASE("Container: (case-insensitive) is blocked", "[sandbox_network]") {
    auto reason = get_blocked_network_mode_reason("Container:my-gateway");
    REQUIRE(reason.has_value());
    CHECK(*reason == NetworkModeBlockReason::ContainerNamespaceJoin);
}

TEST_CASE("bridge mode is safe", "[sandbox_network]") {
    CHECK_FALSE(get_blocked_network_mode_reason("bridge").has_value());
}

TEST_CASE("none mode is safe", "[sandbox_network]") {
    CHECK_FALSE(get_blocked_network_mode_reason("none").has_value());
}

TEST_CASE("custom named network is safe", "[sandbox_network]") {
    CHECK_FALSE(get_blocked_network_mode_reason("my-custom-network").has_value());
}

TEST_CASE("empty mode is safe", "[sandbox_network]") {
    CHECK_FALSE(get_blocked_network_mode_reason("").has_value());
}

// ---------------------------------------------------------------------------
// is_dangerous_network_mode
// ---------------------------------------------------------------------------

TEST_CASE("is_dangerous_network_mode returns true for host", "[sandbox_network]") {
    CHECK(is_dangerous_network_mode("host"));
    CHECK(is_dangerous_network_mode("container:xyz"));
    CHECK_FALSE(is_dangerous_network_mode("bridge"));
    CHECK_FALSE(is_dangerous_network_mode("none"));
}

// ---------------------------------------------------------------------------
// validate_sandbox_network_mode (with break-glass override)
// ---------------------------------------------------------------------------

TEST_CASE("host mode is blocked even with break-glass override", "[sandbox_network]") {
    CHECK_FALSE(validate_sandbox_network_mode("host", true));
    CHECK_FALSE(validate_sandbox_network_mode("host", false));
}

TEST_CASE("container: mode blocked without break-glass", "[sandbox_network]") {
    CHECK_FALSE(validate_sandbox_network_mode("container:abc", false));
}

TEST_CASE("container: mode allowed with break-glass override", "[sandbox_network]") {
    CHECK(validate_sandbox_network_mode("container:abc", true));
}

TEST_CASE("safe modes always pass validation", "[sandbox_network]") {
    CHECK(validate_sandbox_network_mode("bridge", false));
    CHECK(validate_sandbox_network_mode("none", false));
    CHECK(validate_sandbox_network_mode("my-network", false));
}
