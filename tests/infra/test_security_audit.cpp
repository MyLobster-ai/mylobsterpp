#include <catch2/catch_test_macros.hpp>

#include "openclaw/infra/security_audit.hpp"

using namespace openclaw;
using namespace openclaw::infra;

TEST_CASE("collect_multi_user_findings for default config", "[infra][security_audit]") {
    Config config;
    auto findings = collect_multi_user_findings(config);
    CHECK(findings.empty());  // No channels enabled by default
}

TEST_CASE("collect_multi_user_findings warns about open DM policy", "[infra][security_audit]") {
    Config config;
    ChannelConfig tg;
    tg.type = "telegram";
    tg.enabled = true;
    tg.settings = nlohmann::json{
        {"channel_name", "my-telegram"},
        {"dm_policy", "open"},
    };
    config.channels.push_back(std::move(tg));

    auto findings = collect_multi_user_findings(config);
    REQUIRE(!findings.empty());
    CHECK(findings[0].category == "multi_user");
    CHECK(findings[0].severity == "warning");
}

TEST_CASE("collect_multi_user_findings notes group channels", "[infra][security_audit]") {
    Config config;
    ChannelConfig discord;
    discord.type = "discord";
    discord.enabled = true;
    discord.settings = nlohmann::json{{"channel_name", "my-discord"}};
    config.channels.push_back(std::move(discord));

    auto findings = collect_multi_user_findings(config);
    REQUIRE(!findings.empty());
    CHECK(findings[0].category == "multi_user");
    CHECK(findings[0].severity == "info");
}

TEST_CASE("collect_multi_user_findings warns about unsandboxed browser", "[infra][security_audit]") {
    Config config;
    config.browser.enabled = true;
    config.sandbox.enabled = false;

    auto findings = collect_multi_user_findings(config);
    REQUIRE(!findings.empty());
    bool found_tool_exposure = false;
    for (const auto& f : findings) {
        if (f.category == "tool_exposure") found_tool_exposure = true;
    }
    CHECK(found_tool_exposure);
}

TEST_CASE("list_potential_multi_user_signals detects group channels", "[infra][security_audit]") {
    Config config;
    ChannelConfig discord;
    discord.type = "discord";
    discord.enabled = true;
    discord.settings = nlohmann::json{};
    config.channels.push_back(std::move(discord));

    ChannelConfig slack;
    slack.type = "slack";
    slack.enabled = true;
    slack.settings = nlohmann::json{};
    config.channels.push_back(std::move(slack));

    auto signals = list_potential_multi_user_signals(config);
    CHECK(signals.size() >= 2);  // multi-channel + at least one group messaging signal
}

TEST_CASE("collect_risky_tool_exposure for sandboxed config", "[infra][security_audit]") {
    Config config;
    config.browser.enabled = true;
    config.sandbox.enabled = true;

    auto findings = collect_risky_tool_exposure(config);
    CHECK(findings.empty());
}
