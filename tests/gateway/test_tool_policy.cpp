#include <catch2/catch_test_macros.hpp>

#include "openclaw/gateway/tool_policy.hpp"

using namespace openclaw::gateway;

TEST_CASE("ToolPolicy owner-only tools", "[gateway][tool_policy]") {
    ToolPolicy policy;
    policy.set_owner("admin@example.com");

    SECTION("Owner can access owner-only tools") {
        CHECK(policy.is_allowed("whatsapp_login", "admin@example.com"));
    }

    SECTION("Non-owner cannot access owner-only tools") {
        CHECK_FALSE(policy.is_allowed("whatsapp_login", "user@example.com"));
    }

    SECTION("Non-owner can access non-owner tools") {
        CHECK(policy.is_allowed("help", "user@example.com"));
    }
}

TEST_CASE("ToolPolicy group expansion", "[gateway][tool_policy]") {
    auto sessions = ToolPolicy::expand_group("group:sessions");
    CHECK(!sessions.empty());

    bool found_spawn = false;
    bool found_send = false;
    for (const auto& t : sessions) {
        if (t == "spawn") found_spawn = true;
        if (t == "send") found_send = true;
    }
    CHECK(found_spawn);
    CHECK(found_send);

    auto automation = ToolPolicy::expand_group("group:automation");
    CHECK(!automation.empty());

    auto unknown = ToolPolicy::expand_group("group:nonexistent");
    CHECK(unknown.empty());
}

TEST_CASE("ToolPolicy profiles", "[gateway][tool_policy]") {
    SECTION("Minimal profile only allows basic tools") {
        auto tools = ToolPolicy::profile_tools(ToolProfile::Minimal);
        CHECK(tools.contains("help"));
        CHECK(tools.contains("version"));
        CHECK_FALSE(tools.contains("code_search"));
        CHECK_FALSE(tools.contains("spawn"));
    }

    SECTION("Coding profile adds dev tools") {
        auto tools = ToolPolicy::profile_tools(ToolProfile::Coding);
        CHECK(tools.contains("help"));
        CHECK(tools.contains("code_search"));
        CHECK(tools.contains("shell"));
        CHECK_FALSE(tools.contains("spawn"));
    }

    SECTION("Full profile includes everything") {
        auto tools = ToolPolicy::profile_tools(ToolProfile::Full);
        CHECK(tools.contains("help"));
        CHECK(tools.contains("code_search"));
        CHECK(tools.contains("spawn"));
        CHECK(tools.contains("gateway"));
    }
}

TEST_CASE("ToolPolicy allow/deny overrides", "[gateway][tool_policy]") {
    ToolPolicy policy;
    policy.set_profile(ToolProfile::Minimal);

    SECTION("Explicit allow overrides profile restriction") {
        policy.allow("code_search");
        CHECK(policy.is_allowed("code_search", "user"));
    }

    SECTION("Explicit deny overrides everything") {
        policy.deny("help");
        CHECK_FALSE(policy.is_allowed("help", "user"));
    }

    SECTION("Deny takes precedence over allow") {
        policy.allow("shell");
        policy.deny("shell");
        CHECK_FALSE(policy.is_allowed("shell", "user"));
    }
}
