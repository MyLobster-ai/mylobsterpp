#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "openclaw/core/config.hpp"

using namespace openclaw;
using json = nlohmann::json;

TEST_CASE("AgentDefaultParams config (v2026.4.1)", "[config]") {
    AgentDefaultParams defaults;

    SECTION("params defaults to null JSON") {
        CHECK(defaults.params.is_null());
    }

    SECTION("compaction defaults to nullopt") {
        CHECK(!defaults.compaction.has_value());
    }

    SECTION("JSON round-trip") {
        defaults.params = json{{"temperature", 0.7}, {"top_p", 0.9}};
        defaults.compaction = CompactionConfig{};
        defaults.compaction->model = "claude-sonnet-4-6";

        json j = defaults;
        auto restored = j.get<AgentDefaultParams>();
        CHECK(restored.params["temperature"] == 0.7);
        CHECK(restored.params["top_p"] == 0.9);
        CHECK(restored.compaction.has_value());
        CHECK(*restored.compaction->model == "claude-sonnet-4-6");
    }
}

TEST_CASE("AuthCooldownConfig (v2026.4.1)", "[config]") {
    AuthCooldownConfig config;

    SECTION("default rate_limited_profile_rotations is 3") {
        CHECK(config.rate_limited_profile_rotations == 3);
    }

    SECTION("JSON round-trip") {
        config.rate_limited_profile_rotations = 5;
        json j = config;
        auto restored = j.get<AuthCooldownConfig>();
        CHECK(restored.rate_limited_profile_rotations == 5);
    }
}

TEST_CASE("WebchatConfig (v2026.4.1)", "[config]") {
    WebchatConfig config;

    SECTION("chat_history_max_chars defaults to nullopt") {
        CHECK(!config.chat_history_max_chars.has_value());
    }

    SECTION("JSON round-trip with value") {
        config.chat_history_max_chars = 50000;
        json j = config;
        auto restored = j.get<WebchatConfig>();
        CHECK(*restored.chat_history_max_chars == 50000);
    }
}

TEST_CASE("SearxngConfig (v2026.3.31)", "[config]") {
    SearxngConfig config;

    SECTION("disabled by default") {
        CHECK(config.enabled == false);
        CHECK(!config.host.has_value());
    }

    SECTION("JSON round-trip") {
        config.enabled = true;
        config.host = "http://searxng.local:8888";
        json j = config;
        auto restored = j.get<SearxngConfig>();
        CHECK(restored.enabled);
        CHECK(*restored.host == "http://searxng.local:8888");
    }
}

TEST_CASE("TtsDiagnostics config (v2026.3.31/v2026.4.1)", "[config]") {
    TtsDiagnostics config;

    SECTION("defaults") {
        CHECK(config.enabled == false);
        CHECK(config.provider == "system");
        CHECK(!config.voice_wake.has_value());
    }

    SECTION("voice_wake option (v2026.4.1)") {
        config.voice_wake = "talk_mode";
        CHECK(*config.voice_wake == "talk_mode");
    }
}

TEST_CASE("MemoryQmdConfig (v2026.3.31/v2026.4.1)", "[config]") {
    MemoryQmdConfig config;

    SECTION("defaults") {
        CHECK(!config.extra_collections.has_value());
        CHECK(config.match_mode == "mask");  // v2026.4.1: prefer mask over glob
    }

    SECTION("extra_collections for cross-agent search") {
        config.extra_collections = std::vector<std::string>{"agent-2-sessions", "shared-docs"};
        CHECK(config.extra_collections->size() == 2);
    }
}

TEST_CASE("NodeCommandConfig (v2026.3.31)", "[config]") {
    NodeCommandConfig config;

    SECTION("defaults require pairing approval") {
        CHECK(config.require_pairing_approval == true);
        CHECK(config.reduced_trusted_surface == true);
    }
}

TEST_CASE("CronJobToolsConfig (v2026.4.1)", "[config]") {
    CronJobToolsConfig config;

    SECTION("allowed_tools defaults to nullopt") {
        CHECK(!config.allowed_tools.has_value());
    }

    SECTION("per-job tool allowlist") {
        config.allowed_tools = std::vector<std::string>{"web_search", "memory_search"};
        REQUIRE(config.allowed_tools.has_value());
        CHECK(config.allowed_tools->size() == 2);
        CHECK((*config.allowed_tools)[0] == "web_search");
    }
}

TEST_CASE("Config includes v2026.4.1 fields", "[config]") {
    Config config;

    SECTION("new optional fields default to nullopt") {
        CHECK(!config.agent_defaults.has_value());
        CHECK(!config.auth_cooldowns.has_value());
        CHECK(!config.webchat.has_value());
        CHECK(!config.searxng.has_value());
        CHECK(!config.tts.has_value());
        CHECK(!config.memory_qmd.has_value());
        CHECK(!config.node_commands.has_value());
    }

    SECTION("JSON round-trip with all new fields") {
        config.agent_defaults = AgentDefaultParams{};
        config.agent_defaults->params = json{{"temperature", 0.5}};
        config.auth_cooldowns = AuthCooldownConfig{.rate_limited_profile_rotations = 2};
        config.webchat = WebchatConfig{.chat_history_max_chars = 10000};
        config.searxng = SearxngConfig{.host = "http://searx:8080", .enabled = true};
        config.tts = TtsDiagnostics{.enabled = true};
        config.node_commands = NodeCommandConfig{};

        json j = config;
        auto restored = j.get<Config>();
        REQUIRE(restored.agent_defaults.has_value());
        CHECK(restored.agent_defaults->params["temperature"] == 0.5);
        REQUIRE(restored.auth_cooldowns.has_value());
        CHECK(restored.auth_cooldowns->rate_limited_profile_rotations == 2);
        REQUIRE(restored.webchat.has_value());
        CHECK(*restored.webchat->chat_history_max_chars == 10000);
        REQUIRE(restored.searxng.has_value());
        CHECK(restored.searxng->enabled);
        REQUIRE(restored.tts.has_value());
        CHECK(restored.tts->enabled);
        REQUIRE(restored.node_commands.has_value());
        CHECK(restored.node_commands->require_pairing_approval);
    }
}
