#include <catch2/catch_test_macros.hpp>
#include "openclaw/channels/slack.hpp"

using namespace openclaw::channels;

// ---------------------------------------------------------------------------
// SlackConfig new v2026.3.7 fields
// ---------------------------------------------------------------------------

TEST_CASE("SlackConfig typing_reaction defaults to nullopt", "[channels][slack]") {
    SlackConfig config;
    CHECK_FALSE(config.typing_reaction.has_value());
}

TEST_CASE("SlackConfig typing_reaction set to emoji", "[channels][slack]") {
    SlackConfig config;
    config.typing_reaction = "thinking_face";
    CHECK(*config.typing_reaction == "thinking_face");
}

TEST_CASE("SlackConfig typing_reaction various emojis", "[channels][slack]") {
    SlackConfig config;

    SECTION("hourglass") {
        config.typing_reaction = "hourglass_flowing_sand";
        CHECK(*config.typing_reaction == "hourglass_flowing_sand");
    }
    SECTION("speech bubble") {
        config.typing_reaction = "speech_balloon";
        CHECK(*config.typing_reaction == "speech_balloon");
    }
    SECTION("eyes") {
        config.typing_reaction = "eyes";
        CHECK(*config.typing_reaction == "eyes");
    }
}

TEST_CASE("SlackConfig media_local_roots defaults to nullopt", "[channels][slack]") {
    SlackConfig config;
    CHECK_FALSE(config.media_local_roots.has_value());
}

TEST_CASE("SlackConfig media_local_roots with paths", "[channels][slack]") {
    SlackConfig config;
    config.media_local_roots = {{"/home/user/uploads", "/var/data/files"}};
    REQUIRE(config.media_local_roots.has_value());
    CHECK(config.media_local_roots->size() == 2);
    CHECK((*config.media_local_roots)[0] == "/home/user/uploads");
    CHECK((*config.media_local_roots)[1] == "/var/data/files");
}

TEST_CASE("SlackConfig media_local_roots empty list", "[channels][slack]") {
    SlackConfig config;
    config.media_local_roots = std::vector<std::string>{};
    REQUIRE(config.media_local_roots.has_value());
    CHECK(config.media_local_roots->empty());
}

TEST_CASE("SlackConfig streaming_mode from v2026.3.2", "[channels][slack]") {
    SlackConfig config;

    SECTION("default nullopt") {
        CHECK_FALSE(config.streaming_mode.has_value());
    }
    SECTION("partial") {
        config.streaming_mode = "partial";
        CHECK(*config.streaming_mode == "partial");
    }
}

TEST_CASE("SlackConfig thread_session_isolation", "[channels][slack]") {
    SlackConfig config;
    CHECK_FALSE(config.thread_session_isolation);
    config.thread_session_isolation = true;
    CHECK(config.thread_session_isolation);
}

TEST_CASE("SlackConfig all v2026.3.7 fields with v2026.2.x fields", "[channels][slack]") {
    SlackConfig config;
    config.bot_token = "xoxb-test";
    config.app_token = "xapp-test";
    config.use_socket_mode = true;
    config.reply_to_mode = "thread";
    config.channel_allowlist = {"C12345"};
    config.case_insensitive_allowlist = true;
    config.typing_reaction = "thinking_face";
    config.media_local_roots = {{"/uploads"}};
    config.streaming_mode = "partial";
    config.thread_session_isolation = true;
    config.monitor_enabled = false;

    CHECK(config.bot_token == "xoxb-test");
    CHECK(config.channel_allowlist.size() == 1);
    CHECK(*config.typing_reaction == "thinking_face");
    CHECK(config.media_local_roots->size() == 1);
    CHECK_FALSE(config.monitor_enabled);
}
