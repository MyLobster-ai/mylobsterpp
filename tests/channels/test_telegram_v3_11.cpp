#include <catch2/catch_test_macros.hpp>
#include "openclaw/channels/telegram.hpp"

using namespace openclaw::channels;

// ---------------------------------------------------------------------------
// TelegramConfig new v2026.3.7–3.11 fields
// ---------------------------------------------------------------------------

TEST_CASE("TelegramConfig topic bindings defaults", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "test";
    CHECK_FALSE(config.enable_topic_bindings);
    CHECK_FALSE(config.topic_agent_id.has_value());
}

TEST_CASE("TelegramConfig topic binding with agent override", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "test";
    config.enable_topic_bindings = true;
    config.topic_agent_id = "agent-forum-123";

    CHECK(config.enable_topic_bindings);
    CHECK(*config.topic_agent_id == "agent-forum-123");
}

TEST_CASE("TelegramConfig DM streaming draft defaults", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "test";
    CHECK_FALSE(config.dm_streaming_draft);
}

TEST_CASE("TelegramConfig DM streaming draft enabled", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "test";
    config.dm_streaming_draft = true;
    CHECK(config.dm_streaming_draft);
}

TEST_CASE("TelegramConfig proxy URL support", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "test";

    SECTION("default no proxy") {
        CHECK_FALSE(config.proxy_url.has_value());
    }
    SECTION("HTTP proxy") {
        config.proxy_url = "http://proxy.example.com:8080";
        CHECK(*config.proxy_url == "http://proxy.example.com:8080");
    }
    SECTION("SOCKS5 proxy") {
        config.proxy_url = "socks5://10.0.0.1:1080";
        CHECK(*config.proxy_url == "socks5://10.0.0.1:1080");
    }
}

TEST_CASE("TelegramConfig streaming mode from v2026.3.2 preserved", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "test";

    SECTION("default is nullopt") {
        CHECK_FALSE(config.streaming_mode.has_value());
    }
    SECTION("can set to partial (v2026.3.7 default behavior)") {
        config.streaming_mode = "partial";
        CHECK(*config.streaming_mode == "partial");
    }
    SECTION("can set to full") {
        config.streaming_mode = "full";
        CHECK(*config.streaming_mode == "full");
    }
}

TEST_CASE("TelegramConfig reply_to_mode default", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "test";
    CHECK(config.reply_to_mode == "all");
}

TEST_CASE("TelegramConfig disable_audio_preflight", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "test";
    CHECK_FALSE(config.disable_audio_preflight);
    config.disable_audio_preflight = true;
    CHECK(config.disable_audio_preflight);
}

TEST_CASE("TelegramConfig webhook port 0 for ephemeral binding", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "test";
    CHECK(config.webhook_port == 0);
}

TEST_CASE("TelegramConfig all v2026.3.7 fields coexist with v2026.2.x", "[channels][telegram]") {
    TelegramConfig config;
    config.bot_token = "bot123:ABC";
    config.dm_policy = "allowlist";
    config.allowed_sender_ids = {"user1", "user2"};
    config.group_allowlist = {"group1"};
    config.enable_topic_bindings = true;
    config.topic_agent_id = "agent-A";
    config.dm_streaming_draft = true;
    config.proxy_url = "http://proxy:8080";
    config.streaming_mode = "partial";

    CHECK(config.dm_policy == "allowlist");
    CHECK(config.allowed_sender_ids.size() == 2);
    CHECK(config.group_allowlist.size() == 1);
    CHECK(config.enable_topic_bindings);
    CHECK(*config.topic_agent_id == "agent-A");
    CHECK(config.dm_streaming_draft);
    CHECK(*config.proxy_url == "http://proxy:8080");
    CHECK(*config.streaming_mode == "partial");
}
